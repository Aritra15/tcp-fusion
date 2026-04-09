/*
 * tcp-fusion-cwnd.cc
 *
 * Traces the congestion window (cwnd) behaviour of TCP-Fusion over time.
 * Two flows sharing a bottleneck: Flow 1 = specified variant, Flow 2 = TcpNewReno.
 * Outputs per-second cwnd snapshots to files:
 *   fusion_cwnd_flow1.dat   (variant flow)
 *   fusion_cwnd_flow2.dat   (Reno flow)

 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpFusionCwnd");

// --- Global output streams ---
std::ofstream g_cwndFile1;
std::ofstream g_cwndFile2;

void
CwndTracer1(uint32_t oldCwnd, uint32_t newCwnd)
{
    g_cwndFile1 << Simulator::Now().GetSeconds() << " " << newCwnd << std::endl;
}

void
CwndTracer2(uint32_t oldCwnd, uint32_t newCwnd)
{
    g_cwndFile2 << Simulator::Now().GetSeconds() << " " << newCwnd << std::endl;
}

void
TraceCwnd1()
{
    Config::ConnectWithoutContext(
        "/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
        MakeCallback(&CwndTracer1));
}

void
TraceCwnd2()
{
    Config::ConnectWithoutContext(
        "/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
        MakeCallback(&CwndTracer2));
}

int
main(int argc, char* argv[])
{
    std::string tcpVariant = "ns3::TcpFusion";
    double simTime = 100.0;
    double lossRate = 1e-5;
    uint32_t segmentSize = 1448;
    std::string bottleneckBW = "100Mbps";
    std::string bottleneckDelay = "20ms";
    std::string accessBW = "1Gbps";
    std::string accessDelay = "1ms";
    uint32_t bufferSize = 500000;
    std::string outPrefix = "fusion_cwnd";
    std::string outDir = ".";

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpVariant", "TCP variant for flow 1", tcpVariant);
    cmd.AddValue("simTime", "Simulation time (s)", simTime);
    cmd.AddValue("lossRate", "Random loss rate", lossRate);
    cmd.AddValue("segmentSize", "TCP segment size", segmentSize);
    cmd.AddValue("bottleneckBW", "Bottleneck BW", bottleneckBW);
    cmd.AddValue("bottleneckDelay", "Bottleneck delay", bottleneckDelay);
    cmd.AddValue("bufferSize", "Buffer size (bytes)", bufferSize);
    cmd.AddValue("outPrefix", "Output file name prefix (no path)", outPrefix);
    cmd.AddValue("outDir", "Output directory", outDir);
    cmd.Parse(argc, argv);

    // Global defaults: NewReno; sender 0 will be overridden
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName("ns3::TcpNewReno")));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4194304));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(4194304));

    // Nodes
    NodeContainer senders, receivers, routers;
    senders.Create(2);
    receivers.Create(2);
    routers.Create(2);

    // Links
    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue(accessBW));
    accessLink.SetChannelAttribute("Delay", StringValue(accessDelay));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBW));
    bottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneck.SetQueue("ns3::DropTailQueue", "MaxSize",
                        QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSize)));

    NetDeviceContainer s0r0 = accessLink.Install(senders.Get(0), routers.Get(0));
    NetDeviceContainer s1r0 = accessLink.Install(senders.Get(1), routers.Get(0));
    NetDeviceContainer r0r1 = bottleneck.Install(routers.Get(0), routers.Get(1));
    NetDeviceContainer r1d0 = accessLink.Install(routers.Get(1), receivers.Get(0));
    NetDeviceContainer r1d1 = accessLink.Install(routers.Get(1), receivers.Get(1));

    // Internet stack
    InternetStackHelper stack;
    stack.Install(routers);
    stack.Install(receivers);
    stack.Install(senders.Get(1)); // NewReno

    InternetStackHelper stack0;
    stack0.Install(senders.Get(0));

    // Override sender 0 to use the variant
    Config::Set("/NodeList/0/$ns3::TcpL4Protocol/SocketType",
                TypeIdValue(TypeId::LookupByName(tcpVariant)));

    // Traffic control
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize",
                         QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSize)));
    tch.Install(r0r1);

    // IP addresses
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

    // Error model
    if (lossRate > 0.0)
    {
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetAttribute("ErrorRate", DoubleValue(lossRate));
        em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        r0r1.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }

    // Applications
    uint16_t port1 = 50000, port2 = 50001;

    BulkSendHelper src1("ns3::TcpSocketFactory",
                        InetSocketAddress(recvIf0.GetAddress(1), port1));
    src1.SetAttribute("MaxBytes", UintegerValue(0));
    src1.SetAttribute("SendSize", UintegerValue(segmentSize));
    ApplicationContainer app1 = src1.Install(senders.Get(0));
    app1.Start(Seconds(0.1));
    app1.Stop(Seconds(simTime - 0.1));

    PacketSinkHelper sk1("ns3::TcpSocketFactory",
                         InetSocketAddress(Ipv4Address::GetAny(), port1));
    sk1.Install(receivers.Get(0)).Start(Seconds(0.0));

    BulkSendHelper src2("ns3::TcpSocketFactory",
                        InetSocketAddress(recvIf1.GetAddress(1), port2));
    src2.SetAttribute("MaxBytes", UintegerValue(0));
    src2.SetAttribute("SendSize", UintegerValue(segmentSize));
    ApplicationContainer app2 = src2.Install(senders.Get(1));
    app2.Start(Seconds(0.1));
    app2.Stop(Seconds(simTime - 0.1));

    PacketSinkHelper sk2("ns3::TcpSocketFactory",
                         InetSocketAddress(Ipv4Address::GetAny(), port2));
    sk2.Install(receivers.Get(1)).Start(Seconds(0.0));

    // Open cwnd trace files
    std::string path1 = outDir + "/" + outPrefix + "_flow1.dat";
    std::string path2 = outDir + "/" + outPrefix + "_flow2.dat";
    g_cwndFile1.open(path1);
    g_cwndFile2.open(path2);

    // Schedule cwnd tracing after sockets are created (at t=0.2s)
    Simulator::Schedule(Seconds(0.2), &TraceCwnd1);
    Simulator::Schedule(Seconds(0.2), &TraceCwnd2);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    g_cwndFile1.close();
    g_cwndFile2.close();

    std::cout << "CWND traces written to " << path1 << " and "
              << path2 << std::endl;

    Simulator::Destroy();
    return 0;
}
