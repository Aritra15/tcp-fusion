/*

 * Measures throughput of THREE identical flows all using the same TCP variant,
 * sharing a single bottleneck.  
 *
 * Topology: dumbbell — 100 Mbps bottleneck, 20 ms one-way delay,
 *           buffer = BDP (500 KB), TailDrop.  1 Gbps / 1 ms access links.
 *

 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpFusionThreeFlows");

int
main(int argc, char* argv[])
{
    std::string tcpVariant = "ns3::TcpFusion";
    double simTime = 60.0;
    uint32_t segmentSize = 1448;
    std::string bottleneckBW = "100Mbps";
    std::string bottleneckDelay = "20ms";
    std::string accessBW = "1Gbps";
    std::string accessDelay = "1ms";
    uint32_t bufferSize = 500000; // ~BDP at 100Mbps * 40ms
    uint32_t nFlows = 3;
    double lossRate = 0.0;        // default: no random loss (only queue drops)

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpVariant", "TCP variant for ALL flows", tcpVariant);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("segmentSize", "TCP segment size (bytes)", segmentSize);
    cmd.AddValue("bottleneckBW", "Bottleneck bandwidth", bottleneckBW);
    cmd.AddValue("bottleneckDelay", "Bottleneck one-way delay", bottleneckDelay);
    cmd.AddValue("accessBW", "Access link bandwidth", accessBW);
    cmd.AddValue("accessDelay", "Access link delay", accessDelay);
    cmd.AddValue("bufferSize", "Bottleneck buffer size (bytes)", bufferSize);
    cmd.AddValue("nFlows", "Number of identical flows", nFlows);
    cmd.AddValue("lossRate", "Random packet loss rate (0 = none)", lossRate);
    cmd.Parse(argc, argv);

    // Set TCP variant globally — ALL flows use the same algorithm
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName(tcpVariant)));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4194304));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(4194304));

    // --- Topology ---
    //   sender_0 ──┐                  ┌── receiver_0
    //   sender_1 ──┤── router0 ════ router1 ──┤── receiver_1
    //   sender_2 ──┘  (bottleneck)    └── receiver_2

    NodeContainer senders, receivers, routers;
    senders.Create(nFlows);
    receivers.Create(nFlows);
    routers.Create(2);

    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue(accessBW));
    accessLink.SetChannelAttribute("Delay", StringValue(accessDelay));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBW));
    bottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneck.SetQueue("ns3::DropTailQueue", "MaxSize",
                        QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSize)));

    // Connect senders to router0
    std::vector<NetDeviceContainer> senderDevs(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        senderDevs[i] = accessLink.Install(senders.Get(i), routers.Get(0));
    }

    // Bottleneck: router0 — router1
    NetDeviceContainer bnDevs = bottleneck.Install(routers.Get(0), routers.Get(1));

    // Connect router1 to receivers
    std::vector<NetDeviceContainer> recvDevs(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        recvDevs[i] = accessLink.Install(routers.Get(1), receivers.Get(i));
    }

    // Internet stack
    InternetStackHelper stack;
    stack.Install(senders);
    stack.Install(receivers);
    stack.Install(routers);

    // Traffic control on bottleneck
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize",
                         QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSize)));
    tch.Install(bnDevs);

    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    uint32_t subnet = 1;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        std::ostringstream base;
        base << "10.1." << subnet++ << ".0";
        ipv4.SetBase(base.str().c_str(), "255.255.255.0");
        ipv4.Assign(senderDevs[i]);
    }

    {
        std::ostringstream base;
        base << "10.1." << subnet++ << ".0";
        ipv4.SetBase(base.str().c_str(), "255.255.255.0");
        ipv4.Assign(bnDevs);
    }

    std::vector<Ipv4InterfaceContainer> recvIf(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        std::ostringstream base;
        base << "10.1." << subnet++ << ".0";
        ipv4.SetBase(base.str().c_str(), "255.255.255.0");
        recvIf[i] = ipv4.Assign(recvDevs[i]);
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Optional random loss
    if (lossRate > 0.0)
    {
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetAttribute("ErrorRate", DoubleValue(lossRate));
        em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        bnDevs.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }

    // Applications — one BulkSend per flow
    uint16_t basePort = 50000;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        uint16_t port = basePort + i;

        BulkSendHelper src("ns3::TcpSocketFactory",
                           InetSocketAddress(recvIf[i].GetAddress(1), port));
        src.SetAttribute("MaxBytes", UintegerValue(0));
        src.SetAttribute("SendSize", UintegerValue(segmentSize));
        ApplicationContainer app = src.Install(senders.Get(i));
        app.Start(Seconds(0.1 + i * 0.05)); // slight stagger
        app.Stop(Seconds(simTime - 0.1));

        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApp = sink.Install(receivers.Get(i));
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simTime));
    }

    // Flow monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // Compute per-flow throughput
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    // Collect throughput indexed by port
    std::map<uint16_t, double> portThr;
    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
        if (t.destinationPort >= basePort && t.destinationPort < basePort + nFlows)
        {
            double thr = iter.second.rxBytes * 8.0 / (simTime * 1e6); // Mbps
            portThr[t.destinationPort] += thr;
        }
    }

    // Print: flow1 flow2 flow3 ...
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        if (i > 0) std::cout << " ";
        std::cout << std::fixed << std::setprecision(4)
                  << portThr[basePort + i];
    }
    std::cout << std::endl;

    Simulator::Destroy();
    return 0;
}
