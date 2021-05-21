/*
	This file is part of Nanos6 and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2019-2020 Barcelona Supercomputing Center (BSC)
*/

#include <cassert>

#include "ExecutionWorkflow.hpp"
#include "executors/threads/WorkerThread.hpp"
#include "executors/threads/TaskFinalization.hpp"
#include "lowlevel/FatalErrorHandler.hpp"
#include "memory/directory/Directory.hpp"
#include "system/TrackingPoints.hpp"
#include "tasks/TaskImplementation.hpp"
#include "src/memory/directory/Directory.hpp"
#include "scheduling/Scheduler.hpp"

#include <ClusterManager.hpp>
#include <DataAccess.hpp>
#include <DataAccessRegistration.hpp>
#include <DataAccessRegistrationImplementation.hpp>
#include <ExecutionWorkflowHost.hpp>
#include <ExecutionWorkflowCluster.hpp>
#include <ClusterManager.hpp>
#include <InstrumentDependencySubsystemEntryPoints.hpp>

namespace ExecutionWorkflow {

	transfers_map_t _transfersMap = {{
		/*              host         cuda      opencl    cluster   */
		/* host */    { nullCopy,    nullCopy, nullCopy, clusterCopy },
		/* cuda */    { nullCopy,    nullCopy, nullCopy, nullCopy },
		/* opencl */  { nullCopy,    nullCopy, nullCopy, nullCopy },
		/* cluster */ { clusterCopy, nullCopy, nullCopy, clusterCopy }
		}};

	Step *WorkflowBase::createDataCopyStep(
		MemoryPlace const *sourceMemoryPlace,
		MemoryPlace const *targetMemoryPlace,
		DataAccessRegion const &region,
		DataAccess *access,
		bool isTaskwait
	) {
		Step *step;
		Instrument::enterCreateDataCopyStep(isTaskwait);

		/* At the moment we do not support data copies for accesses
			* of the following types. This essentially mean that devices,
			* e.g. Cluster, CUDA, do not support these accesses. */
		if (access->getType() == REDUCTION_ACCESS_TYPE ||
			access->getType() == COMMUTATIVE_ACCESS_TYPE ||
			access->getType() == CONCURRENT_ACCESS_TYPE
		) {
			step = new Step();
			Instrument::exitCreateDataCopyStep(isTaskwait);
			return step;
		}

		assert(targetMemoryPlace != nullptr);
		assert(!Directory::isDirectoryMemoryPlace(targetMemoryPlace));

		// The source memory place is nullptr if and only if the dependency is
		// not yet read satisfied, which is only possible (at this point) if
		// the access is weak. If it is not read satisfied do nothing now:
		// don't copy the data and don't register the dependency. This means
		// for instance that the data will not be eagerly fetched (as
		// controlled by cluster.eager_weak_fetch) and the registration will be
		// done when we receive MessageSatisfiability.
		const nanos6_device_t sourceType =
			(sourceMemoryPlace == nullptr) ? nanos6_host_device : sourceMemoryPlace->getType();

		const nanos6_device_t targetType = targetMemoryPlace->getType();

		/* Starting workflow for a task on the host: not in a namespace */
		if (targetType == nanos6_host_device
			|| targetMemoryPlace == ClusterManager::getCurrentMemoryNode()) {

			access->setValidNamespaceSelf( ClusterManager::getCurrentMemoryNode()->getIndex());
		}

		if (Directory::isDirectoryMemoryPlace(sourceMemoryPlace)
			&& ClusterManager::inClusterMode()) {
			// In cluster mode, if it's in the directory, always use
			// clusterCopy. The data doesn't need copying, since being in the
			// directory implies that the data is uninitialized. But the new
			// location may need registering in the remote dependency system.
			step = clusterCopy(sourceMemoryPlace, targetMemoryPlace, region, access);
		} else {
			step = _transfersMap[sourceType][targetType](
				sourceMemoryPlace,
				targetMemoryPlace,
				region,
				access
			);
		}

		Instrument::exitCreateDataCopyStep(isTaskwait);
		return step;
	}

	Step *WorkflowBase::createExecutionStep(Task *task, ComputePlace *computePlace)
	{
		switch(computePlace->getType()) {
			case nanos6_host_device:
				return new HostExecutionStep(task, computePlace);
			case nanos6_cluster_device:
				return new ClusterExecutionStep(task, computePlace);
			default:
				FatalErrorHandler::failIf(
					true,
					"Execution workflow does not support this device yet"
				);
				return nullptr;
		}
	}

	Step *WorkflowBase::createNotificationStep(
		std::function<void ()> const &callback,
		ComputePlace const *computePlace
	) {
		const nanos6_device_t type =
			(computePlace == nullptr) ? nanos6_host_device : computePlace->getType();

		switch (type) {
			case nanos6_host_device:
				return new HostNotificationStep(callback);
			case nanos6_cluster_device:
				return new ClusterNotificationStep(callback);
			default:
				FatalErrorHandler::failIf(
					true,
					"Execution workflow does not support this device yet"
				);
				//! Silencing annoying compiler warning
				return nullptr;
		}
	}

	DataReleaseStep *WorkflowBase::createDataReleaseStep(Task *task)
	{
		if (task->isRemoteTask()) {
			return new ClusterDataReleaseStep(task->getClusterContext(), task);
		}

		return new DataReleaseStep(task);
	}


	void WorkflowBase::start()
	{
		std::map<MemoryPlace const*, size_t> fragments;
		std::map<MemoryPlace const*, std::vector<ClusterDataCopyStep *>> groups;

		// Iterate over all the rootSteps. There will be null copies
		for (Step *step : _rootSteps) {

			ClusterDataCopyStep *clusterCopy = dynamic_cast<ClusterDataCopyStep *>(step);

			// It is a null copy or some other type.
			if (!clusterCopy) {
				step->start();
				continue;
			}

			// It is a copy step, so group them respect to destination
			// requiresDataFetch will inmediately release successors when
			// (!_needsTransfer && !_isTaskwait)
			if (clusterCopy->requiresDataFetch()) {
				assert(clusterCopy->getTargetMemoryPlace()
					== ClusterManager::getCurrentMemoryNode());

				MemoryPlace const* source = clusterCopy->getSourceMemoryPlace();

				fragments[source] += clusterCopy->getNumFragments();
				groups[source].push_back(clusterCopy);
			}
		}

		for (auto const& it : groups) {
			MemoryPlace const* source = it.first;

			// Instrument::logMessage(
			// 	Instrument::ThreadInstrumentationContext::getCurrent(),
			// 	"ClusterDataCopyStep for:", _region,
			// 	" from Node:", source,
			// 	" to Node:", ClusterManager::getCurrentMemoryNode()
			// );

			ClusterManager::fetchVector(fragments[source], it.second, source);
		}
	}



	void executeTask(Task *task, ComputePlace *targetComputePlace, MemoryPlace *targetMemoryPlace)
	{
		/* The workflow has already been created for this Task.
		 * At this point the Task has been assigned to a WorkerThread
		 * because all its pending DataCopy steps have been completed
		 * and it's ready to actually run.
		 */
		if (task->getWorkflow() != nullptr) {
			ExecutionWorkflow::Step *executionStep = task->getExecutionStep();

			if (executionStep == nullptr) {

				/* Task has already executed and is in a "wait" clause waiting
				 * for its children to complete. The notification step has
				 * already been executed, but markAsFinished returned false.
				 * Now, finally, the wait clause is done, the accesses can be
				 * unregistered and the task disposed. NOTE:
				 * task->getWorkflow() is actually a dangling pointer as the
				 * workflow has already been deleted.
				 */

				assert(task->mustDelayRelease());
				WorkerThread *currThread = WorkerThread::getCurrentWorkerThread();
				CPU * const cpu =
					(currThread != nullptr) ? currThread->getComputePlace() : nullptr;
				CPUDependencyData localDependencyData;
				CPUDependencyData &hpDependencyData =
					(cpu == nullptr) ? localDependencyData : cpu->getDependencyData();

				/*
				 * Continue what was started in Task::markAsFinished, i.e.
				 * everything after Task::markAsBlocked returned false.
				 */
				task->completeDelayedRelease();
				task->markAsUnblocked();
				DataAccessRegistration::handleExitTaskwait(task, cpu, hpDependencyData);

				/*
				 * Now finish the notification step, i.e. everything after
				 * Task::markAsFinished returned false, except that the work
				 * of TaskFinalization::taskFinished(task, cpu) was already done
				 * when a child finished and called TaskFinalization::taskFinished.
				 */
				assert (task->hasFinished());
				DataAccessRegistration::unregisterTaskDataAccesses(
					task,
					cpu, /*cpu, */
					hpDependencyData,
					targetMemoryPlace,
					false,
					/* For clusters, finalize this task and send
					 * the MessageTaskFinished BEFORE propagating
					 * satisfiability to any other tasks. This is to
					 * avoid potentially sending the
					 * MessageTaskFinished messages out of order
					 */
					[&] {
						TaskFinalization::taskFinished(task, cpu);
						bool ret = task->markAsReleased();
						if (ret) {
							TaskFinalization::disposeTask(task);
						}
					}
				);

			} else {
				executionStep->start();
			}

			return;
		}

		//! This is the target MemoryPlace that we will use later on,
		//! once the Task has completed, to update the location of its
		//! DataAccess objects. This can be overriden, if we
		//! release/unregister the accesses passing a different
		//! MemoryPlace.
		task->setMemoryPlace(targetMemoryPlace);

		// int numSymbols = task->getSymbolNum();
		Workflow<TaskExecutionWorkflowData> *workflow =
			new Workflow<TaskExecutionWorkflowData>(0 /* numSymbols */);

		Step *executionStep = workflow->createExecutionStep(task, targetComputePlace);

		Step *notificationStep = workflow->createNotificationStep(
			[=]() {
				WorkerThread *currThread = WorkerThread::getCurrentWorkerThread();

				CPU * const cpu =
					(currThread != nullptr) ? currThread->getComputePlace() : nullptr;

				CPUDependencyData localDependencyData;
				CPUDependencyData &hpDependencyData =
					(cpu == nullptr) ? localDependencyData : cpu->getDependencyData();

				/*
				 * For offloaded tasks with cluster.disable_autowait=false, handle
				 * the early release of dependencies propagated in the namespace. All
				 * other dependencies will be handled using the normal "wait" mechanism.
				 */
				DataAccessRegistration::unregisterLocallyPropagatedTaskDataAccesses(
					task,
					cpu,
					hpDependencyData);

				if (task->markAsFinished(cpu/* cpu */)) {
						DataAccessRegistration::unregisterTaskDataAccesses(
							task,
							cpu, /*cpu, */
							hpDependencyData,
							targetMemoryPlace,
							false,
							/* For clusters, finalize this task and send
							 * the MessageTaskFinished BEFORE propagating
							 * satisfiability to any other tasks. This is to
							 * avoid potentially sending the
							 * MessageTaskFinished messages out of order
							 */
							[&] {
								TaskFinalization::taskFinished(task, cpu);
								bool ret = task->markAsReleased();
								if (ret) {
									// const std::string label = task->getLabel();
									TaskFinalization::disposeTask(task);
								}
							}
						);
					}
				delete workflow;
			},
			targetComputePlace
		);

		/* TODO: Once we have correct management for the Task symbols here
		 * we should create the corresponding allocation steps. */

		DataReleaseStep *releaseStep = workflow->createDataReleaseStep(task);
		workflow->enforceOrder(executionStep, releaseStep);
		workflow->enforceOrder(releaseStep, notificationStep);

		DataAccessRegistration::processAllDataAccesses(
			task,
			[&](DataAccess *dataAccess) -> bool {
				assert(dataAccess != nullptr);
				DataAccessRegion const &region = dataAccess->getAccessRegion();

				MemoryPlace const *currLocation = dataAccess->getLocation();

#ifndef NDEBUG
				// In debug mode, raise an error if the task has a non-weak access to
				// an unknown region.
				if (!dataAccess->isWeak()) {
					if (ClusterManager::inClusterMode()
						&& Directory::isDirectoryMemoryPlace(currLocation)
						&& targetComputePlace->getType() == nanos6_host_device) {

						// This isn't perfect, because the homeNodes list is only empty if the whole
						// region is missing from the directory whereas we would prefer to raise an
						// error even if just a part of it is missing. But this test does a good job
						// of finding blatantly wrong accesses.
						Directory::HomeNodesArray const *homeNodes = Directory::find(region);
						FatalErrorHandler::failIf(
							homeNodes->empty(),
							"Non-weak access ",
							region,
							" of ",
							task->getLabel(),
							" is an unknown region not from lmalloc, dmalloc or the stack");

						delete homeNodes;
					}
				}
#endif
				Step *dataCopyRegionStep = workflow->createDataCopyStep(
					currLocation,
					targetMemoryPlace,
					region,
					dataAccess,
					false
				);

				workflow->enforceOrder(dataCopyRegionStep, executionStep);
				workflow->addRootStep(dataCopyRegionStep);

				releaseStep->addAccess(dataAccess);

				return true;
			}
		);

		if (executionStep->ready()) {
			workflow->enforceOrder(executionStep, notificationStep);
			workflow->addRootStep(executionStep);
		}

		task->setWorkflow(workflow);
		task->setComputePlace(targetComputePlace);

		//! Starting the workflow will either execute the task to
		//! completion (if there are not pending transfers for the
		//! task), or it will setup all the Execution Step will
		//! execute when ready.
		workflow->start();
	}

	void setupTaskwaitWorkflow(
		Task *task,
		DataAccess *taskwaitFragment,
		CPUDependencyData &hpDependencyData
	) {
		Instrument::enterSetupTaskwaitWorkflow();
		WorkerThread *currentThread = WorkerThread::getCurrentWorkerThread();

		ComputePlace *computePlace =
			(currentThread == nullptr) ? nullptr : currentThread->getComputePlace();;

		DataAccessRegion region = taskwaitFragment->getAccessRegion();

		MemoryPlace const *targetLocation = taskwaitFragment->getOutputLocation();

		//! No need to perform any copy for this taskwait fragment
		if (targetLocation == nullptr) {
			DataAccessRegistration::releaseTaskwaitFragment(
				task,
				region,
				computePlace,
				hpDependencyData,
				false);
			Instrument::exitSetupTaskwaitWorkflow();
			return;
		}

		Workflow<DataAccessRegion> *workflow = new Workflow<DataAccessRegion>();


		Step *notificationStep =
			workflow->createNotificationStep(
				[=]() {
					/* We cannot re-use the 'computePlace', we need to
						* retrieve the current Thread and associated
						* ComputePlace */
					WorkerThread *releasingThread = WorkerThread::getCurrentWorkerThread();

					ComputePlace *releasingComputePlace =
						(releasingThread == nullptr) ? nullptr : releasingThread->getComputePlace();

					/* Here, we are always using a local CPUDependencyData
						* object, to avoid the issue where we end-up calling
						* this while the thread is already in the dependency
						* system, using the CPUDependencyData of its
						* ComputePlace. This is a *TEMPORARY* solution, until
						* we fix how we handle taskwaits in a more clean
						* way. */
					CPUDependencyData localDependencyData;

					DataAccessRegistration::releaseTaskwaitFragment(
						task,
						region,
						releasingComputePlace,
						localDependencyData,
						true
					);

					delete workflow;
				},
				computePlace
			);

		MemoryPlace const *currLocation = taskwaitFragment->getLocation();

		Step *copyStep = workflow->createDataCopyStep(
			currLocation,
			targetLocation,
			region,
			taskwaitFragment,
			true
		);

		workflow->addRootStep(copyStep);
		workflow->enforceOrder(copyStep, notificationStep);
		workflow->start();
		Instrument::exitSetupTaskwaitWorkflow();
	}

};
