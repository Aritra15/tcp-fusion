/*
 * Measures throughput of a SINGLE TCP flow at various random loss rates.
 * Topology: dumbbell — 100 Mbps bottleneck, 20 ms one-way delay,
 *           buffer = BDP (500 KB), TailDrop.

 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpFusionSingleFlow");

int
main(int argc, char* argv[])
{
    std::string tcpVariant = "ns3::TcpFusion";
    double lossRate = 1e-4;
    double simTime = 60.0; // seconds
    uint32_t segmentSize = 1448;
    std::string bottleneckBW = "100Mbps";
    std::string bottleneckDelay = "20ms";
    std::string accessBW = "1Gbps";
    std::string accessDelay = "1ms";
    uint32_t bufferSize = 500000; // bytes (~BDP for 100Mbps * 40ms)

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpVariant", "TCP variant (e.g., ns3::TcpFusion)", tcpVariant);
    cmd.AddValue("lossRate", "Random packet loss rate", lossRate);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("segmentSize", "TCP segment size (bytes)", segmentSize);
    cmd.AddValue("bottleneckBW", "Bottleneck bandwidth", bottleneckBW);
    cmd.AddValue("bottleneckDelay", "Bottleneck one-way delay", bottleneckDelay);
    cmd.AddValue("accessBW", "Access link bandwidth", accessBW);
    cmd.AddValue("accessDelay", "Access link delay", accessDelay);
    cmd.AddValue("bufferSize", "Bottleneck buffer size (bytes)", bufferSize);
    cmd.Parse(argc, argv);

    // Set TCP variant globally
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TypeId::LookupByName(tcpVariant)));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4194304));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(4194304));

    // Create nodes: sender -- router1 -- router2 -- receiver
    NodeContainer senderNode, receiverNode, routers;
    senderNode.Create(1);
    receiverNode.Create(1);
    routers.Create(2);

    // Point-to-point links
    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue(accessBW));
    accessLink.SetChannelAttribute("Delay", StringValue(accessDelay));

    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue(bottleneckBW));
    bottleneckLink.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneckLink.SetQueue("ns3::DropTailQueue", "MaxSize",
                            QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSize)));

    NetDeviceContainer senderDevices = accessLink.Install(senderNode.Get(0), routers.Get(0));
    NetDeviceContainer bottleneckDevices = bottleneckLink.Install(routers.Get(0), routers.Get(1));
    NetDeviceContainer receiverDevices = accessLink.Install(routers.Get(1), receiverNode.Get(0));

    // Install internet stack
    InternetStackHelper internet;
    internet.Install(senderNode);
    internet.Install(receiverNode);
    internet.Install(routers);

    // Traffic control: set buffer on bottleneck
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize",
                         QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSize)));
    tch.Install(bottleneckDevices);

    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer senderIf = ipv4.Assign(senderDevices);
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    ipv4.Assign(bottleneckDevices);
    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer receiverIf = ipv4.Assign(receiverDevices);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Add random loss on the bottleneck
    if (lossRate > 0.0)
    {
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetAttribute("ErrorRate", DoubleValue(lossRate));
        em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        bottleneckDevices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }

    // BulkSend application (sender -> receiver)
    uint16_t port = 50000;
    BulkSendHelper source("ns3::TcpSocketFactory",
                          InetSocketAddress(receiverIf.GetAddress(1), port));
    source.SetAttribute("MaxBytes", UintegerValue(0)); // unlimited
    source.SetAttribute("SendSize", UintegerValue(segmentSize));
    ApplicationContainer sourceApp = source.Install(senderNode.Get(0));
    sourceApp.Start(Seconds(0.1));
    sourceApp.Stop(Seconds(simTime - 0.1));

    // PacketSink (receiver)
    PacketSinkHelper sink("ns3::TcpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(receiverNode.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simTime));

    // Flow monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // Compute throughput
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    double totalThroughput = 0.0;
    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
        // Only count forward flows (sender -> receiver)
        if (t.destinationPort == port)
        {
            double thr = iter.second.rxBytes * 8.0 / (simTime * 1e6); // Mbps
            totalThroughput += thr;
        }
    }

    std::cout << lossRate << " " << totalThroughput << std::endl;

    Simulator::Destroy();
    return 0;
}
