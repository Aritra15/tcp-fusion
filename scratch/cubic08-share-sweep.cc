/*
 *
 * Recreates the style of CUBIC'08 Fig.10 experiments:
 * - 4 flows of tested variant + 4 flows of TCP NewReno (SACK proxy)
 * - aggregate throughput shares measured at bottleneck
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
#include <map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Cubic08ShareSweep");

int
main(int argc, char* argv[])
{
    std::string tcpVariant = "ns3::TcpCubic";
    uint32_t nVariantFlows = 4;
    uint32_t nRenoFlows = 4;
    double simTime = 80.0;
    double rttMs = 40.0;
    double bandwidthMbps = 400.0;
    uint32_t segmentSize = 1448;
    uint32_t bufferSizeBytes = 1000000;

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpVariant", "TCP variant for first flow group", tcpVariant);
    cmd.AddValue("nVariantFlows", "Number of tested-variant flows", nVariantFlows);
    cmd.AddValue("nRenoFlows", "Number of NewReno flows", nRenoFlows);
    cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
    cmd.AddValue("rttMs", "Target RTT in ms", rttMs);
    cmd.AddValue("bandwidthMbps", "Bottleneck bandwidth in Mbps", bandwidthMbps);
    cmd.AddValue("segmentSize", "TCP segment size in bytes", segmentSize);
    cmd.AddValue("bufferSizeBytes", "Bottleneck queue size in bytes", bufferSizeBytes);
    cmd.Parse(argc, argv);

    const uint32_t nFlows = nVariantFlows + nRenoFlows;

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 22));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 22));

    NodeContainer senders, receivers, routers;
    senders.Create(nFlows);
    receivers.Create(nFlows);
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

    std::vector<NetDeviceContainer> senderLinks;
    senderLinks.reserve(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        senderLinks.push_back(access.Install(senders.Get(i), routers.Get(0)));
    }

    NetDeviceContainer b = btl.Install(routers.Get(0), routers.Get(1));

    std::vector<NetDeviceContainer> receiverLinks;
    receiverLinks.reserve(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        receiverLinks.push_back(access.Install(routers.Get(1), receivers.Get(i)));
    }

    InternetStackHelper stack;
    stack.Install(senders);
    stack.Install(receivers);
    stack.Install(routers);

    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize",
                         QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, bufferSizeBytes)));
    tch.Install(b);

    Ipv4AddressHelper ipv4;
    uint32_t subnet = 1;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        std::ostringstream net;
        net << "10.1." << subnet++ << ".0";
        ipv4.SetBase(net.str().c_str(), "255.255.255.0");
        ipv4.Assign(senderLinks[i]);
    }

    {
        std::ostringstream net;
        net << "10.1." << subnet++ << ".0";
        ipv4.SetBase(net.str().c_str(), "255.255.255.0");
        ipv4.Assign(b);
    }

    std::vector<Ipv4InterfaceContainer> recvIf;
    recvIf.reserve(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        std::ostringstream net;
        net << "10.1." << subnet++ << ".0";
        ipv4.SetBase(net.str().c_str(), "255.255.255.0");
        recvIf.push_back(ipv4.Assign(receiverLinks[i]));
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Assign TCP variant by sender node index.
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        uint32_t nodeId = senders.Get(i)->GetId();
        if (i < nVariantFlows)
        {
            Config::Set("/NodeList/" + std::to_string(nodeId) + "/$ns3::TcpL4Protocol/SocketType",
                        TypeIdValue(TypeId::LookupByName(tcpVariant)));
        }
        else
        {
            Config::Set("/NodeList/" + std::to_string(nodeId) + "/$ns3::TcpL4Protocol/SocketType",
                        TypeIdValue(TcpNewReno::GetTypeId()));
        }
    }

    const uint16_t variantBasePort = 50000;
    const uint16_t renoBasePort = 51000;

    for (uint32_t i = 0; i < nFlows; ++i)
    {
        bool isVariant = i < nVariantFlows;
        uint16_t port = isVariant ? (variantBasePort + i) : (renoBasePort + (i - nVariantFlows));

        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        auto sinkApp = sink.Install(receivers.Get(i));
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simTime));

        BulkSendHelper src("ns3::TcpSocketFactory",
                           InetSocketAddress(recvIf[i].GetAddress(1), port));
        src.SetAttribute("MaxBytes", UintegerValue(0));
        src.SetAttribute("SendSize", UintegerValue(segmentSize));
        auto app = src.Install(senders.Get(i));
        app.Start(Seconds(1.0 + 0.02 * i));
        app.Stop(Seconds(simTime - 0.1));
    }

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> mon = flowmon.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    mon->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    auto stats = mon->GetFlowStats();

    double variantMbps = 0.0;
    double renoMbps = 0.0;

    for (const auto& kv : stats)
    {
        auto tuple = classifier->FindFlow(kv.first);
        double thr = kv.second.rxBytes * 8.0 / (simTime * 1e6);
        if (tuple.destinationPort >= variantBasePort &&
            tuple.destinationPort < static_cast<uint16_t>(variantBasePort + nVariantFlows))
        {
            variantMbps += thr;
        }
        else if (tuple.destinationPort >= renoBasePort &&
                 tuple.destinationPort < static_cast<uint16_t>(renoBasePort + nRenoFlows))
        {
            renoMbps += thr;
        }
    }

    std::cout << std::fixed << std::setprecision(4)
              << variantMbps << " " << renoMbps << std::endl;

    Simulator::Destroy();
    return 0;
}
