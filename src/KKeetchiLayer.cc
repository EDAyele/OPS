//
// The model implementation for the Keetchi layer
//
// @author : Asanga Udugama (adu@comnets.uni-bremen.de)
// @date   : 12-oct-2015
//
//

#include "KKeetchiLayer.h"

Define_Module(KKeetchiLayer);

void KKeetchiLayer::initialize(int stage)
{

    if (stage == 0) {
        // get parameters
        ownMACAddress = par("ownMACAddress").stringValue();
        agingInterval = par("agingInterval");

        // set other parameters
        broadcastMACAddress = "FF:FF:FF:FF:FF:FF";
        
    } else if (stage == 1) {

        // create Keetchi API
        keetchiAPI = new KLKeetchi(KLKEETCHI_CACHE_REPLACEMENT_POLICY_LRU, 10, ownMACAddress);

    } else if (stage == 2) {

        // setup the trigger to age data
        ageDataTimeoutEvent = new cMessage("Age Data Timeout Event");
        ageDataTimeoutEvent->setKind(108);
        scheduleAt(simTime() + agingInterval, ageDataTimeoutEvent);

    } else {
        EV_FATAL << KKEETCHILAYER_SIMMODULEINFO << "Something is radically wrong in initialization \n";
    }
}

int KKeetchiLayer::numInitStages() const
{
    return 3;
}

void KKeetchiLayer::handleMessage(cMessage *msg)
{
    cGate *gate;
    char gateName[64];
    KRegisterAppMsg *regAppMsg;
    KDataMsg *omnetDataMsg;
    KFeedbackMsg *omnetFeedbackMsg;
    KNeighbourListMsg *neighListMsg;

    // self messages
    if (msg->isSelfMessage()) {

        // age data trigger fired
        if (msg->getKind() == 108) {

            keetchiAPI->ageData(simTime().dbl());

            // setup next age data trigger
            scheduleAt(simTime() + agingInterval, msg);

        } else {
            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << "Received unexpected self message" << "\n";
            delete msg;
        }

    // messages from other layers
    } else {

       // get message arrival gate name
        gate = msg->getArrivalGate();
        strcpy(gateName, gate->getName());

        // app registration message arrived from the upper layer (app layer)
        if (strstr(gateName, "upperLayerIn") != NULL && (regAppMsg = dynamic_cast<KRegisterAppMsg*>(msg)) != NULL) {

            // application registration request
            keetchiAPI->registerApplication(regAppMsg->getAppName(), regAppMsg->getPrefixName(), simTime().dbl());
            delete msg;

        // data message arrived from the upper layer (app layer)
        } else if (strstr(gateName, "upperLayerIn") != NULL && (omnetDataMsg = dynamic_cast<KDataMsg*>(msg)) != NULL) {

            processUpperLayerInDataMsg(omnetDataMsg);

        // feedback message arrived from the upper layer (app layer)
        } else if (strstr(gateName, "upperLayerIn") != NULL && (omnetFeedbackMsg = dynamic_cast<KFeedbackMsg*>(msg)) != NULL) {

            processUpperLayerInFeedbackMsg(omnetFeedbackMsg);

        // neighbour list message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && (neighListMsg = dynamic_cast<KNeighbourListMsg*>(msg)) != NULL) {

            processLowerLayerInNeighbourListMsg(neighListMsg);

        // data message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && (omnetDataMsg = dynamic_cast<KDataMsg*>(msg)) != NULL) {

            processLowerLayerInDataMsg(omnetDataMsg);

        // feedback message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && (omnetFeedbackMsg = dynamic_cast<KFeedbackMsg*>(msg)) != NULL) {

            processLowerLayerInFeedbackMsg(omnetFeedbackMsg);

        // received some unexpected packet
        } else {

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << "Received unexpected packet" << "\n";
            delete msg;
        }
    }
}

void KKeetchiLayer::finish()
{
    // remove age data trigger
    cancelEvent(ageDataTimeoutEvent);
    delete ageDataTimeoutEvent;

}

// Message processing methods

void KKeetchiLayer::processUpperLayerInDataMsg(KDataMsg *dataMsg)
{
    KDataMsg *omnetDataMsg;
    KFeedbackMsg *omnetFeedbackMsg;
    KLAction *keetchiActions;
    KLFeedbackMsg *keetchiFeedbackMsg;
    KLDataMsg *keetchiDataMsg;

    EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper In :: Data Msg :: " << dataMsg->getSourceAddress() << " :: "
        << dataMsg->getDestinationAddress() << " :: " << dataMsg->getDataName() << " :: " << dataMsg->getGoodnessValue() <<"\n";

    // create the Keetchi API Data message
    keetchiDataMsg = createKeetchiAPIDataMsgFromOMNETDataMsg(dataMsg,
                                KLDATAMSG_MSG_DIRECTION_IN,
                                KLDATAMSG_FROM_APP_LAYER,
                                KLDATAMSG_FROM_TO_NOT_SET);


    // call keetchi API with Data msg to process
    keetchiActions = keetchiAPI->processDataMsg(KLKEETCHI_FROM_APP_LAYER, keetchiDataMsg, simTime().dbl());

    // processing returned a data message to send out
    if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL
            && keetchiActions->getActionType() == KLACTION_ACTION_TYPE_DATAMSG) {

        // create omnet data message
        keetchiDataMsg = keetchiActions->getDataMsg();
        omnetDataMsg = createOMNETDataMsgFromKeetchiAPIDataMsg(keetchiDataMsg);

        if (keetchiDataMsg->getToWhere() == KLDATAMSG_TO_APP_LAYER) {
            send(omnetDataMsg, "upperLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
                << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

        } else { // KLDATAMSG_TO_LINK_LAYER

            send(omnetDataMsg, "lowerLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
                << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

        }
        delete keetchiDataMsg;

    // processing returned a feedback message to send out
    } else if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL
            && keetchiActions->getActionType() == KLACTION_ACTION_TYPE_FEEDBACKMSG) {

        // create omnet feedback message
        keetchiFeedbackMsg = keetchiActions->getFeedbackMsg();
        omnetFeedbackMsg = createOMNETFeedbackMsgFromKeetchiAPIFeedbackMsg(keetchiFeedbackMsg);

        if (keetchiFeedbackMsg->getToWhere() == KLFEEDBACKMSG_TO_APP_LAYER) {
            send(omnetFeedbackMsg, "upperLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Feedback Msg :: " << omnetFeedbackMsg->getSourceAddress() << " :: "
                << omnetFeedbackMsg->getDestinationAddress() << " :: " << omnetFeedbackMsg->getDataName() << " :: " << omnetFeedbackMsg->getGoodnessValue() << "\n";


        } else { // KLFEEDBACKMSG_TO_LINK_LAYER
            send(omnetFeedbackMsg, "lowerLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Feedback Msg :: " << omnetFeedbackMsg->getSourceAddress() << " :: "
                << omnetFeedbackMsg->getDestinationAddress() << " :: " << omnetFeedbackMsg->getDataName() << " :: " << omnetFeedbackMsg->getGoodnessValue()<< "\n";

        }
        delete keetchiFeedbackMsg;

    // processing was successful and no message to send
    } else if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL) {


    // Processing failed
    } else {
        EV_INFO << KKEETCHILAYER_SIMMODULEINFO << "UpperLayerIn data message processing failed " << "\n";

    }

    delete keetchiActions;
    delete dataMsg;

}

void KKeetchiLayer::processUpperLayerInFeedbackMsg(KFeedbackMsg *feedbackMsg)
{
    KDataMsg *omnetDataMsg;
    KFeedbackMsg *omnetFeedbackMsg;
    KLAction *keetchiActions;
    KLFeedbackMsg *keetchiFeedbackMsg;
    KLDataMsg *keetchiDataMsg;


    EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper In :: Feedback Msg :: " << feedbackMsg->getSourceAddress() << " :: "
        << feedbackMsg->getDestinationAddress() << " :: " << feedbackMsg->getDataName() << " :: " << feedbackMsg->getGoodnessValue() << "\n";


    // create the Keetchi API Feedback message
    keetchiFeedbackMsg = createKeetchiAPIFeedbackMsgFromOMNETFeedbackMsg(feedbackMsg,
                                KLFEEDBACKMSG_MSG_DIRECTION_IN,
                                KLFEEDBACKMSG_FROM_APP_LAYER,
                                KLFEEDBACKMSG_FROM_TO_NOT_SET);

    // call keetchi API with Feedback msg to process
    keetchiActions = keetchiAPI->processFeedbackMsg(KLKEETCHI_FROM_APP_LAYER, keetchiFeedbackMsg, simTime().dbl());

    // processing returned a data message to send out
    if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL
            && keetchiActions->getActionType() == KLACTION_ACTION_TYPE_DATAMSG) {

        // create omnet data message
        keetchiDataMsg = keetchiActions->getDataMsg();
        omnetDataMsg = createOMNETDataMsgFromKeetchiAPIDataMsg(keetchiDataMsg);

        if (keetchiDataMsg->getToWhere() == KLDATAMSG_TO_APP_LAYER) {
            send(omnetDataMsg, "upperLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
                << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue()<< "\n";

        } else { // KLDATAMSG_TO_LINK_LAYER
            send(omnetDataMsg, "lowerLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
                << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

        }
        delete keetchiDataMsg;

    // processing returned a feedback message to send out
    } else if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL
            && keetchiActions->getActionType() == KLACTION_ACTION_TYPE_FEEDBACKMSG) {

        // create omnet feedback message
        keetchiFeedbackMsg = keetchiActions->getFeedbackMsg();
        omnetFeedbackMsg = createOMNETFeedbackMsgFromKeetchiAPIFeedbackMsg(keetchiFeedbackMsg);

        if (keetchiFeedbackMsg->getToWhere() == KLFEEDBACKMSG_TO_APP_LAYER) {
            send(omnetFeedbackMsg, "upperLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Feedback Msg :: " << omnetFeedbackMsg->getSourceAddress() << " :: "
                << omnetFeedbackMsg->getDestinationAddress() << " :: " << omnetFeedbackMsg->getDataName() << " :: " << omnetFeedbackMsg->getGoodnessValue() << "\n";

        } else { // KLFEEDBACKMSG_TO_LINK_LAYER
            send(omnetFeedbackMsg, "lowerLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Feedback Msg :: " << omnetFeedbackMsg->getSourceAddress() << " :: "
                << omnetFeedbackMsg->getDestinationAddress() << " :: " << omnetFeedbackMsg->getDataName() << " :: " << omnetFeedbackMsg->getGoodnessValue() << "\n";

        }
        delete keetchiFeedbackMsg;

    // processing was successful and no message to send
    } else if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL) {



    // Processing failed
    } else {
        EV_INFO << KKEETCHILAYER_SIMMODULEINFO << "UpperLayerIn feedback message processing failed " << "\n";

    }

    delete keetchiActions;
    delete feedbackMsg;

}

void KKeetchiLayer::processLowerLayerInNeighbourListMsg(KNeighbourListMsg *neighListMsg)
{
    list<KLAction*> keetchiActionsList;
    list<KLNodeInfo*> keetchiNodeInfoList;
    list<KLAction*>::iterator iteratorKeetchiActions;
    KDataMsg *omnetDataMsg;
    KFeedbackMsg *omnetFeedbackMsg;
    KLAction *keetchiActions;
    KLFeedbackMsg *keetchiFeedbackMsg;
    KLDataMsg *keetchiDataMsg;


    EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: Got neighbourhood msg :: Count :: " << neighListMsg->getNeighbourNameListArraySize() << " \n";

    // build the keetchi node info list
    for (int i = 0; i < neighListMsg->getNeighbourNameListArraySize(); i++) {
        string nodeName = neighListMsg->getNeighbourNameList(i);
        // EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: node name :: " << nodeName << "\n";
        KLNodeInfo* keetchiNodeInfo = new KLNodeInfo(nodeName);
        keetchiNodeInfoList.push_front(keetchiNodeInfo);

    }

    // call Keetchi API with neighbour list to process
    keetchiActionsList = keetchiAPI->processNewNeighbourList(keetchiNodeInfoList, simTime().dbl());

    // loop thru all action objects and send them out
    iteratorKeetchiActions = keetchiActionsList.begin();
    while (iteratorKeetchiActions != keetchiActionsList.end()) {
        keetchiActions = *iteratorKeetchiActions;

        // processing returned a data message to send out
        if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL
                && keetchiActions->getActionType() == KLACTION_ACTION_TYPE_DATAMSG) {

            // create omnet data message
            keetchiDataMsg = keetchiActions->getDataMsg();
            omnetDataMsg = createOMNETDataMsgFromKeetchiAPIDataMsg(keetchiDataMsg);

            if (keetchiDataMsg->getToWhere() == KLDATAMSG_TO_APP_LAYER) {
                send(omnetDataMsg, "upperLayerOut");

                EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
                    << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

            } else { // KLDATAMSG_TO_LINK_LAYER
                send(omnetDataMsg, "lowerLayerOut");

                EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
                    << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

            }
            delete keetchiDataMsg;

        // processing returned a feedback message to send out
        } else if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL
                && keetchiActions->getActionType() == KLACTION_ACTION_TYPE_FEEDBACKMSG) {

            // create omnet feedback message
            keetchiFeedbackMsg = keetchiActions->getFeedbackMsg();
            omnetFeedbackMsg = createOMNETFeedbackMsgFromKeetchiAPIFeedbackMsg(keetchiFeedbackMsg);

            if (keetchiFeedbackMsg->getToWhere() == KLFEEDBACKMSG_TO_APP_LAYER) {
                send(omnetFeedbackMsg, "upperLayerOut");

                EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Feedback Msg :: " << omnetFeedbackMsg->getSourceAddress() << " :: "
                    << omnetFeedbackMsg->getDestinationAddress() << " :: " << omnetFeedbackMsg->getDataName() << " :: " << omnetFeedbackMsg->getGoodnessValue() << "\n";

            } else { // KLFEEDBACKMSG_TO_LINK_LAYER
                send(omnetFeedbackMsg, "lowerLayerOut");

                EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Feedback Msg :: " << omnetFeedbackMsg->getSourceAddress() << " :: "
                    << omnetFeedbackMsg->getDestinationAddress() << " :: " << omnetFeedbackMsg->getDataName() << " :: " << omnetFeedbackMsg->getGoodnessValue() << "\n";

            }
            delete keetchiFeedbackMsg;

        } else {
            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << "LowerLayerIn neighbour list message processing failed " << "\n";

        }

        delete keetchiActions;
        iteratorKeetchiActions++;
    }
    keetchiActionsList.clear();
    delete neighListMsg;
}

void KKeetchiLayer::processLowerLayerInDataMsg(KDataMsg *dataMsg)
{
    KDataMsg *omnetDataMsg;
    KFeedbackMsg *omnetFeedbackMsg;
    KLAction *keetchiActions;
    KLFeedbackMsg *keetchiFeedbackMsg;
    KLDataMsg *keetchiDataMsg;

    EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower In :: Data Msg :: " << dataMsg->getSourceAddress() << " :: "
        << dataMsg->getDestinationAddress() << " :: " << dataMsg->getDataName() << " :: " << dataMsg->getGoodnessValue() << "\n";

    // create the Keetchi API Data message
    keetchiDataMsg = createKeetchiAPIDataMsgFromOMNETDataMsg(dataMsg,
                                KLDATAMSG_MSG_DIRECTION_IN,
                                KLDATAMSG_FROM_LINK_LAYER,
                                KLDATAMSG_FROM_TO_NOT_SET);

    // call keetchi API with Data msg to process
    keetchiActions = keetchiAPI->processDataMsg(KLKEETCHI_FROM_LINK_LAYER, keetchiDataMsg, simTime().dbl());

    // processing returned a data message to send out
    if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL
            && keetchiActions->getActionType() == KLACTION_ACTION_TYPE_DATAMSG) {

        // create omnet data message
        keetchiDataMsg = keetchiActions->getDataMsg();
        omnetDataMsg = createOMNETDataMsgFromKeetchiAPIDataMsg(keetchiDataMsg);

        if (keetchiDataMsg->getToWhere() == KLDATAMSG_TO_APP_LAYER) {
            send(omnetDataMsg, "upperLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
                << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

        } else { // KLDATAMSG_TO_LINK_LAYER
            send(omnetDataMsg, "lowerLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
                << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

        }
        delete keetchiDataMsg;

    // processing returned a feedback message to send out
    } else if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL
            && keetchiActions->getActionType() == KLACTION_ACTION_TYPE_FEEDBACKMSG) {

        // create omnet feedback message
        keetchiFeedbackMsg = keetchiActions->getFeedbackMsg();
        omnetFeedbackMsg = createOMNETFeedbackMsgFromKeetchiAPIFeedbackMsg(keetchiFeedbackMsg);

        if (keetchiFeedbackMsg->getToWhere() == KLFEEDBACKMSG_TO_APP_LAYER) {
            send(omnetFeedbackMsg, "upperLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Feedback Msg :: " << omnetFeedbackMsg->getSourceAddress() << " :: "
                << omnetFeedbackMsg->getDestinationAddress() << " :: " << omnetFeedbackMsg->getDataName() << " :: " << omnetFeedbackMsg->getGoodnessValue() << "\n";

        } else { // KLFEEDBACKMSG_TO_LINK_LAYER
            send(omnetFeedbackMsg, "lowerLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Feedback Msg :: " << omnetFeedbackMsg->getSourceAddress() << " :: "
                << omnetFeedbackMsg->getDestinationAddress() << " :: " << omnetFeedbackMsg->getDataName() << " :: " << omnetFeedbackMsg->getGoodnessValue() << "\n";

        }
        delete keetchiFeedbackMsg;

    // processing was successful and no message to send
    } else if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL) {


    // Processing failed
    } else {
        EV_INFO << KKEETCHILAYER_SIMMODULEINFO << "LowerLayerIn data message processing failed " << "\n";

    }

    delete keetchiActions;
    delete dataMsg;


}

void KKeetchiLayer::processLowerLayerInFeedbackMsg(KFeedbackMsg *feedbackMsg)
{
    KDataMsg *omnetDataMsg;
    KFeedbackMsg *omnetFeedbackMsg;
    KLAction *keetchiActions;
    KLFeedbackMsg *keetchiFeedbackMsg;
    KLDataMsg *keetchiDataMsg;

    EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower In :: Feedback Msg :: " << feedbackMsg->getSourceAddress() << " :: "
        << feedbackMsg->getDestinationAddress() << " :: " << feedbackMsg->getDataName() << " :: " << feedbackMsg->getGoodnessValue() << "\n";

    // create the Keetchi API Feedback message
    keetchiFeedbackMsg = createKeetchiAPIFeedbackMsgFromOMNETFeedbackMsg(feedbackMsg,
                                KLFEEDBACKMSG_MSG_DIRECTION_IN,
                                KLFEEDBACKMSG_FROM_LINK_LAYER,
                                KLFEEDBACKMSG_FROM_TO_NOT_SET);

    // call keetchi API with Feedback msg to process
    keetchiActions = keetchiAPI->processFeedbackMsg(KLKEETCHI_FROM_LINK_LAYER, keetchiFeedbackMsg, simTime().dbl());

    // processing returned a data message to send out
    if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL
            && keetchiActions->getActionType() == KLACTION_ACTION_TYPE_DATAMSG) {

        // create omnet data message
        keetchiDataMsg = keetchiActions->getDataMsg();
        omnetDataMsg = createOMNETDataMsgFromKeetchiAPIDataMsg(keetchiDataMsg);

        if (keetchiDataMsg->getToWhere() == KLDATAMSG_TO_APP_LAYER) {
            send(omnetDataMsg, "upperLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
                << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

        } else { // KLDATAMSG_TO_LINK_LAYER
            send(omnetDataMsg, "lowerLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
                << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

        }
        delete keetchiDataMsg;

    // processing returned a feedback message to send out
    } else if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL
            && keetchiActions->getActionType() == KLACTION_ACTION_TYPE_FEEDBACKMSG) {

        // create omnet feedback message
        keetchiFeedbackMsg = keetchiActions->getFeedbackMsg();
        omnetFeedbackMsg = createOMNETFeedbackMsgFromKeetchiAPIFeedbackMsg(keetchiFeedbackMsg);

        if (keetchiFeedbackMsg->getToWhere() == KLFEEDBACKMSG_TO_APP_LAYER) {
            send(omnetFeedbackMsg, "upperLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Feedback Msg :: " << omnetFeedbackMsg->getSourceAddress() << " :: "
                << omnetFeedbackMsg->getDestinationAddress() << " :: " << omnetFeedbackMsg->getDataName() << " :: " << omnetFeedbackMsg->getGoodnessValue() << "\n";

        } else { // KLFEEDBACKMSG_TO_LINK_LAYER
            send(omnetFeedbackMsg, "lowerLayerOut");

            EV_INFO << KKEETCHILAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Feedback Msg :: " << omnetFeedbackMsg->getSourceAddress() << " :: "
                << omnetFeedbackMsg->getDestinationAddress() << " :: " << omnetFeedbackMsg->getDataName() << " :: " << omnetFeedbackMsg->getGoodnessValue() << "\n";

        }
        delete keetchiFeedbackMsg;


    // processing was successful and no message to send
    } else if (keetchiActions->getProcessingStatus() == KLACTION_MSG_PROCESSING_SUCCESSFUL) {


    // Processing failed
    } else {
        EV_INFO << KKEETCHILAYER_SIMMODULEINFO << "LowerLayerIn feedback message processing failed " << "\n";

    }

    delete keetchiActions;
    delete feedbackMsg;
}


//
// Message conversion methods
//
KLDataMsg* KKeetchiLayer::createKeetchiAPIDataMsgFromOMNETDataMsg(KDataMsg* omnetDataMsg, int msgDirection, int fromWhere, int toWhere)
{
    char *dataPtr;
    int dataSize;
    const char *dataPayload;
    KLDataMsg* keetchiAPIDataMsg;

    keetchiAPIDataMsg = new KLDataMsg();

    keetchiAPIDataMsg->setMsgDirection(msgDirection);
    keetchiAPIDataMsg->setFromWhere(fromWhere);
    keetchiAPIDataMsg->setToWhere(toWhere);
    keetchiAPIDataMsg->setSourceAddress(omnetDataMsg->getSourceAddress());
    keetchiAPIDataMsg->setDestinationAddress(omnetDataMsg->getDestinationAddress());
    keetchiAPIDataMsg->setDataName(omnetDataMsg->getDataName());
    dataPayload = omnetDataMsg->getDummyPayloadContent();

    dataSize = strlen(dataPayload) + 1;
    if (dataSize > 0) {
        dataPtr = (char *) malloc(dataSize);
        strcpy(dataPtr, dataPayload);
        keetchiAPIDataMsg->setDataPayload(dataPtr, dataSize);
        free(dataPtr);

    } else {
        keetchiAPIDataMsg->setDataPayload((char *)"", 0);

    }
    keetchiAPIDataMsg->setGoodnessValue(omnetDataMsg->getGoodnessValue());
    keetchiAPIDataMsg->setMsgType(omnetDataMsg->getMsgType());
    keetchiAPIDataMsg->setValidUntilTime(omnetDataMsg->getValidUntilTime());


    return keetchiAPIDataMsg;
}

KLFeedbackMsg* KKeetchiLayer::createKeetchiAPIFeedbackMsgFromOMNETFeedbackMsg(KFeedbackMsg* omnetFeedbackMsg, int msgDirection, int fromWhere, int toWhere)
{
    KLFeedbackMsg* keetchiAPIFeedbackMsg;

    keetchiAPIFeedbackMsg = new KLFeedbackMsg();

    keetchiAPIFeedbackMsg->setMsgDirection(msgDirection);
    keetchiAPIFeedbackMsg->setFromWhere(fromWhere);
    keetchiAPIFeedbackMsg->setToWhere(toWhere);
    keetchiAPIFeedbackMsg->setSourceAddress(omnetFeedbackMsg->getSourceAddress());
    keetchiAPIFeedbackMsg->setDestinationAddress(omnetFeedbackMsg->getDestinationAddress());
    keetchiAPIFeedbackMsg->setDataName(omnetFeedbackMsg->getDataName());
    keetchiAPIFeedbackMsg->setGoodnessValue(omnetFeedbackMsg->getGoodnessValue());

    return keetchiAPIFeedbackMsg;
}

KDataMsg* KKeetchiLayer::createOMNETDataMsgFromKeetchiAPIDataMsg(KLDataMsg* keetchiAPIDataMsg)
{
    char *dataPtr;
    KDataMsg* omnetDataMsg;

    omnetDataMsg = new KDataMsg();
    omnetDataMsg->setSourceAddress(keetchiAPIDataMsg->getSourceAddress().c_str());
    if (keetchiAPIDataMsg->getDestinationAddress() == "all") {
        omnetDataMsg->setDestinationAddress(broadcastMACAddress.c_str());        
    } else {
        omnetDataMsg->setDestinationAddress(keetchiAPIDataMsg->getDestinationAddress().c_str());
    }
    omnetDataMsg->setDataName(keetchiAPIDataMsg->getDataName().c_str());
    omnetDataMsg->setGoodnessValue(keetchiAPIDataMsg->getGoodnessValue());

    omnetDataMsg->setMsgType(keetchiAPIDataMsg->getMsgType());
    omnetDataMsg->setValidUntilTime(keetchiAPIDataMsg->getValidUntilTime());
    omnetDataMsg->setRealPayloadSize(keetchiAPIDataMsg->getDataPayloadSize());
    dataPtr = keetchiAPIDataMsg->getDataPayload();
    omnetDataMsg->setDummyPayloadContent(dataPtr);
    omnetDataMsg->setRealPacketSize(592);

    omnetDataMsg->setByteLength(592);

    return omnetDataMsg;
}

KFeedbackMsg* KKeetchiLayer::createOMNETFeedbackMsgFromKeetchiAPIFeedbackMsg(KLFeedbackMsg* keetchiAPIFeedbackMsg)
{
    KFeedbackMsg* omnetFeedbackMsg;

    omnetFeedbackMsg = new KFeedbackMsg();
    omnetFeedbackMsg->setSourceAddress(keetchiAPIFeedbackMsg->getSourceAddress().c_str());
    if (keetchiAPIFeedbackMsg->getDestinationAddress() == "all") {
        omnetFeedbackMsg->setDestinationAddress(broadcastMACAddress.c_str());
    } else {
        omnetFeedbackMsg->setDestinationAddress(keetchiAPIFeedbackMsg->getDestinationAddress().c_str());
    }
    omnetFeedbackMsg->setDataName(keetchiAPIFeedbackMsg->getDataName().c_str());
    omnetFeedbackMsg->setGoodnessValue(keetchiAPIFeedbackMsg->getGoodnessValue());
    omnetFeedbackMsg->setRealPacketSize(78);

    omnetFeedbackMsg->setByteLength(78);

    return omnetFeedbackMsg;
}

