#include <boost/dynamic_bitset.hpp>

#include "hardware/HardwareInfo.hpp"

#include "CPU.hpp"
#include "CPUManager.hpp"
#include "ThreadManager.hpp"

std::vector<CPU *> CPUManager::_cpus;
size_t CPUManager::_totalCPUs;
std::atomic<bool> CPUManager::_finishedCPUInitialization;
SpinLock CPUManager::_idleCPUsLock;
boost::dynamic_bitset<> CPUManager::_idleCPUs;

void CPUManager::preinitialize()
{
	_finishedCPUInitialization = false;
	_totalCPUs = 0;

	cpu_set_t processCPUMask;
	int rc = sched_getaffinity(0, sizeof(cpu_set_t), &processCPUMask);
	FatalErrorHandler::handle(rc, " when retrieving the affinity of the process");

	// Get CPU objects that can run a thread
	std::vector<ComputePlace *> cpus = HardwareInfo::getComputeNodes();
	_cpus.resize(cpus.size());
	for (size_t i = 0; i < cpus.size(); ++i) {
		if (CPU_ISSET(((CPU *)cpus[i])->_systemCPUId, &processCPUMask)) {
			_cpus[i] = (CPU *)cpus[i];
			++_totalCPUs;
		}
	}

	// Set all CPUs as not idle
	_idleCPUs.resize(_cpus.size());
	_idleCPUs.reset();
}

void CPUManager::initialize()
{
	for (size_t i = 0; i < _cpus.size(); ++i) {
		if (_cpus[i] != nullptr) {
			ThreadManager::initializeThread(_cpus[i]);
		}
	}

	_finishedCPUInitialization = true;
}