/*
	This file is part of Nanos6 and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2015-2020 Barcelona Supercomputing Center (BSC)
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Scheduler.hpp"
#include "SchedulerGenerator.hpp"
#include "executors/threads/CPUManager.hpp"
#include "lowlevel/EnvironmentVariable.hpp"
#include "system/RuntimeInfo.hpp"

EnvironmentVariable<std::string> SchedulerInterface::_schedulingPolicy("NANOS6_SCHEDULING_POLICY", "fifo");
EnvironmentVariable<bool> SchedulerInterface::_enableImmediateSuccessor("NANOS6_IMMEDIATE_SUCCESSOR", "1");
EnvironmentVariable<bool> SchedulerInterface::_enablePriority("NANOS6_PRIORITY", "1");


SchedulerInterface::SchedulerInterface()
{
	RuntimeInfo::addEntry("schedulingPolicy", "SchedulingPolicy", _schedulingPolicy);

	SchedulingPolicy policy = FIFO_POLICY;
	if (_schedulingPolicy.getValue() == "LIFO" || _schedulingPolicy.getValue() == "lifo") {
		policy = LIFO_POLICY;
	}

	size_t computePlaceCount;
	computePlaceCount = CPUManager::getTotalCPUs();
	_hostScheduler = SchedulerGenerator::createHostScheduler(
		computePlaceCount, policy, _enablePriority,
		_enableImmediateSuccessor);

	size_t totalDevices = (nanos6_device_t::nanos6_device_type_num);

	for (size_t i = 0; i < totalDevices; i++) {
		_deviceSchedulers[i] = nullptr;
	}

#if USE_CUDA
	computePlaceCount = HardwareInfo::getComputePlaceCount(nanos6_cuda_device);
	_deviceSchedulers[nanos6_cuda_device] =
		SchedulerGenerator::createDeviceScheduler(
			computePlaceCount, policy, _enablePriority,
			_enableImmediateSuccessor, nanos6_cuda_device);
#endif
#if USE_OPENACC
	computePlaceCount = HardwareInfo::getComputePlaceCount(nanos6_openacc_device);
	_deviceSchedulers[nanos6_openacc_device] =
		SchedulerGenerator::createDeviceScheduler(
			computePlaceCount, policy, _enablePriority,
			_enableImmediateSuccessor, nanos6_openacc_device);
#endif
#if NANOS6_OPENCL
	FatalErrorHandler::failIf(true, "OpenCL is not supported yet.");
#endif
#if USE_FPGA
	FatalErrorHandler::failIf(true, "FPGA is not supported yet.");
#endif
}

SchedulerInterface::~SchedulerInterface()
{
	delete _hostScheduler;
#if USE_CUDA
	delete _deviceSchedulers[nanos6_cuda_device];
#endif
#if USE_OPENACC
	delete _deviceSchedulers[nanos6_openacc_device];
#endif
#if NANOS6_OPENCL
	FatalErrorHandler::failIf(true, "OpenCL is not supported yet.");
#endif
#if USE_FPGA
	FatalErrorHandler::failIf(true, "FPGA is not supported yet.");
#endif
}
