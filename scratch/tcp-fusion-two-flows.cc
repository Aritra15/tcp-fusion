/*
 * Measures throughput of TWO coexisting TCP flows with different loss rates.
 * Flow 1 uses the specified tcpVariant, Flow 2 uses TcpNewReno.
 * Topology: dumbbell — 100 Mbps bottleneck, 20 ms one-way delay.
 *
 * Usage:
 *   ./ns3 run "tcp-fusion-two-flows --tcpVariant=ns3::TcpFusion
 *              --lossRate=1e-4 --simTime=60"
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpFusionTwoFlows");

int
main(int argc, char* argv[])
{
    std::string tcpVariant = "ns3::TcpFusion";
    double lossRate = 1e-4;
    double simTime = 60.0;
    uint32_t segmentSize = 1448;
    std::string bottleneckBW = "100Mbps";
    std::string bottleneckDelay = "20ms";
    std::string accessBW = "1Gbps";
    std::string accessDelay = "1ms";
    uint32_t bufferSize = 500000;

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpVariant", "TCP variant for flow 1 (e.g., ns3::TcpFusion)", tcpVariant);
    cmd.AddValue("lossRate", "Random packet loss rate", lossRate);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("segmentSize", "TCP segment size (bytes)", segmentSize);
    cmd.AddValue("bottleneckBW", "Bottleneck bandwidth", bottleneckBW);
    cmd.AddValue("bottleneckDelay", "Bottleneck one-way delay", bottleneckDelay);
    cmd.AddValue("accessBW", "Access link bandwidth", accessBW);
    cmd.AddValue("accessDelay", "Access link delay", accessDelay);
    cmd.AddValue("bufferSize", "Bottleneck buffer size (bytes)", bufferSize);
    cmd.Parse(argc, argv);

    // Default TCP = NewReno globally; we'll override per-socket for flow 1
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName("ns3::TcpNewReno")));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4194304));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(4194304));

    // --- Topology: 2 senders -- router1 -- router2 -- 2 receivers ---
    NodeContainer senders, receivers, routerNodes;
    senders.Create(2);
    receivers.Create(2);
    routerNodes.Create(2);

    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue(accessBW));
    accessLink.SetChannelAttribute("Delay", StringValue(accessDelay));

    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue(bottleneckBW));
    bottleneckLink.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneckLink.SetQueue("ns3::DropTailQueue", "MaxSize",
                            QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSize)));

    // Sender 0 -> Router 0
    NetDeviceContainer s0r0 = accessLink.Install(senders.Get(0), routerNodes.Get(0));
    // Sender 1 -> Router 0
    NetDeviceContainer s1r0 = accessLink.Install(senders.Get(1), routerNodes.Get(0));
    // Router 0 -> Router 1 (bottleneck)
    NetDeviceContainer r0r1 = bottleneckLink.Install(routerNodes.Get(0), routerNodes.Get(1));
    // Router 1 -> Receiver 0
    NetDeviceContainer r1d0 = accessLink.Install(routerNodes.Get(1), receivers.Get(0));
    // Router 1 -> Receiver 1
    NetDeviceContainer r1d1 = accessLink.Install(routerNodes.Get(1), receivers.Get(1));

    // Install internet stack
    // Sender 0 gets the variant TCP, Sender 1 gets NewReno
    InternetStackHelper stackVariant;
    stackVariant.Install(routerNodes);
    stackVariant.Install(receivers);
    stackVariant.Install(senders.Get(1)); // NewReno for sender 1

    // For sender 0, set the variant
    InternetStackHelper stackSender0;
    stackSender0.Install(senders.Get(0));

    // Override TCP type for sender 0 specifically
    Config::Set("/NodeList/0/$ns3::TcpL4Protocol/SocketType",
                TypeIdValue(TypeId::LookupByName(tcpVariant)));

    // Traffic control on bottleneck
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize",
                         QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSize)));
    tch.Install(r0r1);

    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    ipv4.Assign(s0r0);
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    ipv4.Assign(s1r0);
    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    ipv4.Assign(r0r1);
    ipv4.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer recvIf0 = ipv4.Assign(r1d0);
    ipv4.SetBase("10.1.5.0", "255.255.255.0");
    Ipv4InterfaceContainer recvIf1 = ipv4.Assign(r1d1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Random loss on bottleneck
    if (lossRate > 0.0)
    {
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetAttribute("ErrorRate", DoubleValue(lossRate));
        em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        r0r1.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }

    // Flow 1: variant TCP (sender 0 -> receiver 0)
    uint16_t port1 = 50000;
    BulkSendHelper source1("ns3::TcpSocketFactory",
                           InetSocketAddress(recvIf0.GetAddress(1), port1));
    source1.SetAttribute("MaxBytes", UintegerValue(0));
    source1.SetAttribute("SendSize", UintegerValue(segmentSize));
    ApplicationContainer app1 = source1.Install(senders.Get(0));
    app1.Start(Seconds(0.1));
    app1.Stop(Seconds(simTime - 0.1));

    PacketSinkHelper sink1("ns3::TcpSocketFactory",
                           InetSocketAddress(Ipv4Address::GetAny(), port1));
    ApplicationContainer sinkApp1 = sink1.Install(receivers.Get(0));
    sinkApp1.Start(Seconds(0.0));
    sinkApp1.Stop(Seconds(simTime));

    // Flow 2: NewReno (sender 1 -> receiver 1)
    uint16_t port2 = 50001;
    BulkSendHelper source2("ns3::TcpSocketFactory",
                           InetSocketAddress(recvIf1.GetAddress(1), port2));
    source2.SetAttribute("MaxBytes", UintegerValue(0));
    source2.SetAttribute("SendSize", UintegerValue(segmentSize));
    ApplicationContainer app2 = source2.Install(senders.Get(1));
    app2.Start(Seconds(0.1));
    app2.Stop(Seconds(simTime - 0.1));

    PacketSinkHelper sink2("ns3::TcpSocketFactory",
                           InetSocketAddress(Ipv4Address::GetAny(), port2));
    ApplicationContainer sinkApp2 = sink2.Install(receivers.Get(1));
    sinkApp2.Start(Seconds(0.0));
    sinkApp2.Stop(Seconds(simTime));

    // Flow monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // Compute throughput per flow
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    double variantThr = 0.0, renoThr = 0.0;
    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
        double thr = iter.second.rxBytes * 8.0 / (simTime * 1e6);
        if (t.destinationPort == port1)
        {
            variantThr += thr;
        }
        else if (t.destinationPort == port2)
        {
            renoThr += thr;
        }
    }

    std::cout << lossRate << " " << variantThr << " " << renoThr << std::endl;

    Simulator::Destroy();
    return 0;
}
