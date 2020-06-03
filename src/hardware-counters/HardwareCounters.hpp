/*
	This file is part of Nanos6 and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2020 Barcelona Supercomputing Center (BSC)
*/

#ifndef HARDWARE_COUNTERS_HPP
#define HARDWARE_COUNTERS_HPP

#include <vector>

#include "HardwareCountersInterface.hpp"
#include "SupportedHardwareCounters.hpp"
#include "lowlevel/EnvironmentVariable.hpp"
#include "lowlevel/FatalErrorHandler.hpp"


class Task;

class HardwareCounters {

private:

	//! Whether the verbose mode is enabled
	static EnvironmentVariable<bool> _verbose;

	//! The file where output must be saved when verbose mode is enabled
	static EnvironmentVariable<std::string> _verboseFile;

	//! The underlying PQoS backend
	static HardwareCountersInterface *_pqosBackend;

	//! The underlying PAPI backend
	static HardwareCountersInterface *_papiBackend;

	//! Whether there is at least one enabled backend
	static bool _anyBackendEnabled;

	//! Whether each backend is enabled
	static std::vector<bool> _enabled;

	//! Enabled events by the user (id, description)
	static std::vector<bool> _enabledEvents;

private:

	//! \brief Load backend and counter enabling configuration from the default
	//! configuration file
	static void loadConfigurationFile();

	//! \brief Check if two or more backends are enabled and incompatible
	static inline void checkIncompatibleBackends()
	{
		if (_enabled[HWCounters::PAPI_BACKEND] && _enabled[HWCounters::PQOS_BACKEND]) {
			FatalErrorHandler::fail("PAPI and PQoS are incompatible hardware counter libraries");
		}
	}

public:

	//! \brief Initialize the hardware counters API with the correct backend
	static void initialize();

	//! \brief Shutdown the hardware counters API
	static void shutdown();

	//! \brief Check whether a backend is enabled
	//!
	//! \param[in] backend The backend's id
	static inline bool isBackendEnabled(HWCounters::backends_t backend)
	{
		return _enabled[backend];
	}

	//! \brief Get a vector of enabled events, where the index is an event type
	//! (HWCounters::counters_t) and the boolean tells wether it is enabled
	static inline const std::vector<bool> &getEnabledCounters()
	{
		return _enabledEvents;
	}

	//! \brief Initialize hardware counter structures for a new thread
	static void threadInitialized();

	//! \brief Destroy the hardware counter structures of a thread
	static void threadShutdown();

	//! \brief Initialize hardware counter structures for a task
	//!
	//! \param[out] task The task to create structures for
	//! \param[in] enabled Whether to create structures and monitor this task
	static void taskCreated(Task *task, bool enabled = true);

	//! \brief Reinitialize all hardware counter structures for a task
	//!
	//! \param[out] task The task to reinitialize structures for
	static void taskReinitialized(Task *task);

	//! \brief Start reading hardware counters for a task
	//!
	//! \param[out] task The task to start hardware counter monitoring for
	static void taskStarted(Task *task);

	//! \brief Stop reading hardware counters for a task
	//!
	//! \param[out] task The task to stop hardware counters monitoring for
	static void taskStopped(Task *task);

	//! \brief Finish monitoring a task's hardware counters and accumulate them
	//!
	//! \param[out] task The task to finish hardware counters monitoring for
	static void taskFinished(Task *task);

};

#endif // HARDWARE_COUNTERS_HPP