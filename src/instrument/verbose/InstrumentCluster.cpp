/*
	This file is part of Nanos6 and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2018-2019 Barcelona Supercomputing Center (BSC)
*/

#include "InstrumentCluster.hpp"
#include "InstrumentVerbose.hpp"

#include <Message.hpp>

using namespace Instrument::Verbose;

namespace Instrument {

	void clusterSendMessage(Message const *msg, int receiverId)
	{
		if (!_verboseClusterMessages) {
			return;
		}

		InstrumentationContext const &context = ThreadInstrumentationContext::getCurrent();

		LogEntry *logEntry = getLogEntry(context);
		assert(logEntry != nullptr);

		logEntry->appendLocation(context);

		// If not receiverId then it is the end of the event.
		if (receiverId >= 0) {
			logEntry->_contents << " --> SendClusterMessage "
				<< msg->getName()
				<< " id:" << msg->getId() << " "
				<< msg->toString()
				<< " targetNode:" << receiverId;
		} else {
			logEntry->_contents << " <-- SendClusterMessage id:" << msg->getId();
		}

		addLogEntry(logEntry);
	}

	void clusterHandleMessage(Message const *msg, int senderId)
	{
		if (!_verboseClusterMessages) {
			return;
		}

		InstrumentationContext const &context = ThreadInstrumentationContext::getCurrent();

		LogEntry *logEntry = getLogEntry(context);
		assert(logEntry != nullptr);

		logEntry->appendLocation(context);

		if (senderId >= 0) {
			logEntry->_contents << " --> HandleClusterMessage "
				<< msg->getName()
				<< " id:" << msg->getId() << " "
				<< msg->toString()
				<< " sourceNode:" << senderId;
		} else {
			logEntry->_contents << " <-- HandleClusterMessage id:" <<  msg->getId();
		}

		addLogEntry(logEntry);
	}

	void clusterDataSend(void *, size_t, int, InstrumentationContext const &)
	{
	}

	void clusterDataReceived(void *, size_t, int, InstrumentationContext const &)
	{
	}

	void taskIsOffloaded(task_id_t, InstrumentationContext const &)
	{
	}

	void stateNodeNamespace(int state, InstrumentationContext const &context)
	{
		std::string status;

		// TODO: This needs an enum probably. Now changes here imply changes in extrae version
		switch (state) {
			case 2:
				status = "Block";
				break;
			case 3:
				status = "Unblock";
				break;
			case 0:
				status = "Finish";
				break;
			case 1:
				status = "Init";
				break;
			default:
				status = "UNKNOWN!!";
		}

		LogEntry *logEntry = getLogEntry(context);
		assert(logEntry != nullptr);

		logEntry->appendLocation(context);
		logEntry->_contents << status << " NodeNamespace task";

		addLogEntry(logEntry);
	}

	void emitClusterEvent(ClusterEventType, int, InstrumentationContext const &)
	{
	}

	void offloadedTaskCompletes(task_id_t, InstrumentationContext const &)
	{
	}
}
