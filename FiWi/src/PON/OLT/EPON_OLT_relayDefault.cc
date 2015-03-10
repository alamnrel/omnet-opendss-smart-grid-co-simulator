//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#include "EPON_OLT_relayDefault.h"

#include <iostream>
#include <fstream>
#include "StatsCollector.h"
#include "PONUtil.h"

Define_Module(EPON_OLT_relayDefault);

EPON_OLT_relayDefault::~EPON_OLT_relayDefault(){
}

void EPON_OLT_relayDefault::initialize()
{
	MACVlanRelayUnitBase::initialize();

	// ONU table
	onutbl = NULL;
	onutbl = dynamic_cast<ONUTable *>( findModuleUp("onuTable"));
	if (!onutbl)
		error("Shit... no ONU table found ?!?!");


    numProcessedFrames = numDroppedFrames = 0;
    WATCH(numProcessedFrames);
    WATCH(numDroppedFrames);

    delayTDM = 0;
    delayWDM = 0;
    throughput = 0;
}

void EPON_OLT_relayDefault::handleMessage(cMessage *msg)
{
	// Self Message
	if (msg->isSelfMessage())
	{
		EV << "Self-message " << msg << " received\n";
		delete msg;
		error("Unknown self message received!");
		return;
	}

	// Network Message
	cGate *ingate = msg->getArrivalGate();
	EV << "Frame " << msg << " arrived on port " << ingate->getName() << "...\n";

	EtherFrame *frame = check_and_cast<EtherFrame *>(msg);

	if (ingate->getId() ==  gate( "toPONin")->getId())
	{
		handleFromPON(frame);
	}
	else if (ingate->getId() ==  gate( "ethIn")->getId()){
		handleFromLAN(frame);
	}
	else{
		EV << "Message received in UNKNOWN PORT\n";
		return;
	}

}

void EPON_OLT_relayDefault::handleFromPON(EtherFrame *frame){
	mac_llid ml;
	ml.llid=-1;

	//Check frame preamble (dist between ONUs ONLY)
	EPON_LLidCtrlInfo * nfo = dynamic_cast<EPON_LLidCtrlInfo *>(frame->removeControlInfo());

	if (simTime() > 2)
	{
		int ch = nfo ? nfo->channel : 0;

		//if (PONUtil::getPonVersion(this) == 2)
		//	ch = 1; // no TDM !


		EV << "EPON_OLT_relayDefault::handleFromPON received on channel " << ch << endl;

		if (ch == 0)
		{
			delayTDM += (simTime() - nfo->createdAt).dbl();
			++nbDelayTDM;
			StatsCollector::Instance().addStat("delayTDM", STAT_TYPE_MEAN, simTime().dbl(), (simTime() - nfo->createdAt).dbl());
		}
		else
		if (ch > 0)
		{
			delayWDM += (simTime() - nfo->createdAt).dbl();
			++nbDelayWDM;
			StatsCollector::Instance().addStat("delayWDM", STAT_TYPE_MEAN, simTime().dbl(), (simTime() - nfo->createdAt).dbl());
		}

		StatsCollector::Instance().addStat("throughput", STAT_TYPE_PER_SECOND, simTime().dbl(), (double)frame->getBitLength());
		EV << "OLT relay received frame of size -> " << frame->getBitLength() << endl;
		throughput += (double)frame->getBitLength();
	}

	if (nfo!=NULL)
	{
		if (nfo->llid != LLID_EPON_BC)
			ml.llid = nfo->llid;
	}

	delete nfo;

	ml.mac = frame->getSrc();
	updateTableWithAddress(ml, "toPONout");

	// We do not need to scan the table since we still
	// have no vlans. Each ONU is considered to be a different
	// net (subnet) so the frame must go directly to wire.
	send(frame, "ethOut");
}

void EPON_OLT_relayDefault::handleFromLAN(EtherFrame *frame){
	updateTableFromFrame(frame);

	mac_llid ml;
	// Check table ... if port is the same as incoming drop
	ml.mac = frame->getDest();
	std::string port = getPortForAddress(ml);
	if (port == "ethOut") {
		delete frame;
		return;
	}

	// Do MAPINGS HERE (vlan or anything other to LLID)
	// currently use the default
	std::vector<port_llid> vec = getLLIDsForAddress(frame->getDest());

	if (vec.size()!=0){
		// Send to the known llids only
		for (uint32_t i = 0; i<vec.size(); i++){
			EtherFrame *frame_tmp = frame->dup();

			// Get this llid and this port, duplicate and send
			port_llid tmp_pl = (port_llid)vec[i];
			// If we don't know ... lets BC
			if (tmp_pl.llid == -1) tmp_pl.llid = LLID_EPON_BC;

			EV << "Adding LLID to the frame info... : "<<ml.llid<<endl;

			if (frame_tmp->getControlInfo() != NULL)
				frame_tmp->removeControlInfo();

			frame_tmp->setControlInfo(new EPON_LLidCtrlInfo(tmp_pl.llid, 0) ); // TODO copy channel...
			send(frame_tmp, tmp_pl.port.c_str());

			// Break after a BC do not flood the net
			if (tmp_pl.llid == LLID_EPON_BC) break;
		}
		delete frame;
	}else{
		// Add LLID info... to be used in MAC Layer
		EV << "Adding BC LLID to the frame info... "<<endl;
		ml.llid = LLID_EPON_BC;
		EPON_LLidCtrlInfo * nfo = new EPON_LLidCtrlInfo(ml.llid, 0); // TODO copy channel

		if (frame->getControlInfo() != NULL)
			frame->removeControlInfo();

		frame->setControlInfo(nfo);
		send(frame, "toPONout");
	}

}

cModule * EPON_OLT_relayDefault::findModuleUp(const char * name)
{
	cModule *mod = NULL;
	for (cModule *curmod=this; !mod && curmod; curmod=curmod->getParentModule())
	     mod = curmod->getSubmodule(name);
	return mod;
}

void EPON_OLT_relayDefault::finish()
{
}


