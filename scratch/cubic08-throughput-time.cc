/*
 * Figure similar to CUBIC'08 Fig.5 style, but plotting throughput vs time:
 * one high-speed TCP flow (default CUBIC) competing with one NewReno flow.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <fstream>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Cubic08ThroughputTime");

static Ptr<PacketSink> g_variantSink;
static Ptr<PacketSink> g_renoSink;
static uint64_t g_prevVariantRx = 0;
static uint64_t g_prevRenoRx = 0;
static double g_sampleInterval = 1.0;
static std::ofstream g_out;

static void
SampleThroughput()
{
    double t = Simulator::Now().GetSeconds();

    uint64_t varRx = g_variantSink ? g_variantSink->GetTotalRx() : 0;
    uint64_t renoRx = g_renoSink ? g_renoSink->GetTotalRx() : 0;

    double varMbps = (varRx - g_prevVariantRx) * 8.0 / (g_sampleInterval * 1e6);
    double renoMbps = (renoRx - g_prevRenoRx) * 8.0 / (g_sampleInterval * 1e6);

    g_out << std::fixed << std::setprecision(3)
          << t << " " << varMbps << " " << renoMbps << std::endl;

    g_prevVariantRx = varRx;
    g_prevRenoRx = renoRx;

    Simulator::Schedule(Seconds(g_sampleInterval), &SampleThroughput);
}

int
main(int argc, char* argv[])
{
    std::string tcpVariant = "ns3::TcpCubic";
    double simTime = 120.0;
    double rttMs = 8.0;
    double bandwidthMbps = 400.0;
    uint32_t segmentSize = 1448;
    uint32_t bufferSizeBytes = 1000000;
    double sampleInterval = 1.0;
    std::string outFile = "scratch/tcp_fusion_results/cubic_fig5_rtt8.dat";

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpVariant", "TCP variant for flow-1 (e.g., ns3::TcpCubic)", tcpVariant);
    cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
    cmd.AddValue("rttMs", "Target round-trip-time in ms", rttMs);
    cmd.AddValue("bandwidthMbps", "Bottleneck bandwidth in Mbps", bandwidthMbps);
    cmd.AddValue("segmentSize", "TCP segment size in bytes", segmentSize);
    cmd.AddValue("bufferSizeBytes", "Bottleneck queue size in bytes", bufferSizeBytes);
    cmd.AddValue("sampleInterval", "Throughput sampling interval in seconds", sampleInterval);
    cmd.AddValue("outFile", "Output .dat file path", outFile);
    cmd.Parse(argc, argv);

    g_sampleInterval = sampleInterval;

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",TypeIdValue(TcpNewReno::GetTypeId()));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 22));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 22));

    NodeContainer senders;
    senders.Create(2);
    NodeContainer receivers;
    receivers.Create(2);
    NodeContainer routers;
    routers.Create(2);

    double oneWayBtlDelayMs = std::max(0.1, rttMs / 2.0 - 2.0);

    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
    access.SetChannelAttribute("Delay", StringValue("1ms"));

    PointToPointHelper btl;
    btl.SetDeviceAttribute("DataRate", StringValue(std::to_string(bandwidthMbps) + "Mbps"));
    btl.SetChannelAttribute("Delay", StringValue(std::to_string(oneWayBtlDelayMs) + "ms"));
    btl.SetQueue("ns3::DropTailQueue", "MaxSize",
                 QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSizeBytes)));

    NetDeviceContainer s0 = access.Install(senders.Get(0), routers.Get(0));
    NetDeviceContainer s1 = access.Install(senders.Get(1), routers.Get(0));
    NetDeviceContainer b = btl.Install(routers.Get(0), routers.Get(1));
    NetDeviceContainer r0 = access.Install(routers.Get(1), receivers.Get(0));
    NetDeviceContainer r1 = access.Install(routers.Get(1), receivers.Get(1));

    InternetStackHelper stack;
    stack.Install(senders);
    stack.Install(receivers);
    stack.Install(routers);

    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize",
                         QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSizeBytes)));
    tch.Install(b);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    ipv4.Assign(s0);
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    ipv4.Assign(s1);
    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    ipv4.Assign(b);
    ipv4.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer if0 = ipv4.Assign(r0);
    ipv4.SetBase("10.1.5.0", "255.255.255.0");
    Ipv4InterfaceContainer if1 = ipv4.Assign(r1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Variant on sender[0]
    Config::Set("/NodeList/0/$ns3::TcpL4Protocol/SocketType",
                TypeIdValue(TypeId::LookupByName(tcpVariant)));
    // NewReno on sender[1]
    Config::Set("/NodeList/1/$ns3::TcpL4Protocol/SocketType",
                TypeIdValue(TcpNewReno::GetTypeId()));

    uint16_t p0 = 50000;
    uint16_t p1 = 50001;

    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), p0));
    ApplicationContainer sinkApp0 = sinkHelper.Install(receivers.Get(0));
    sinkApp0.Start(Seconds(0.0));
    sinkApp0.Stop(Seconds(simTime));

    sinkHelper.SetAttribute("Local", AddressValue(InetSocketAddress(Ipv4Address::GetAny(), p1)));
    ApplicationContainer sinkApp1 = sinkHelper.Install(receivers.Get(1));
    sinkApp1.Start(Seconds(0.0));
    sinkApp1.Stop(Seconds(simTime));

    BulkSendHelper src0("ns3::TcpSocketFactory", InetSocketAddress(if0.GetAddress(1), p0));
    src0.SetAttribute("MaxBytes", UintegerValue(0));
    src0.SetAttribute("SendSize", UintegerValue(segmentSize));
    ApplicationContainer a0 = src0.Install(senders.Get(0));
    a0.Start(Seconds(1.0));
    a0.Stop(Seconds(simTime - 0.1));

    BulkSendHelper src1("ns3::TcpSocketFactory", InetSocketAddress(if1.GetAddress(1), p1));
    src1.SetAttribute("MaxBytes", UintegerValue(0));
    src1.SetAttribute("SendSize", UintegerValue(segmentSize));
    ApplicationContainer a1 = src1.Install(senders.Get(1));
    a1.Start(Seconds(1.0));
    a1.Stop(Seconds(simTime - 0.1));

    g_variantSink = DynamicCast<PacketSink>(sinkApp0.Get(0));
    g_renoSink = DynamicCast<PacketSink>(sinkApp1.Get(0));

    g_out.open(outFile.c_str(), std::ios::out | std::ios::trunc);
    g_out << "# time_s variant_mbps reno_mbps" << std::endl;

    Simulator::Schedule(Seconds(1.0 + sampleInterval), &SampleThroughput);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    if (g_out.is_open())
    {
        g_out.close();
    }

    Simulator::Destroy();
    return 0;
}
