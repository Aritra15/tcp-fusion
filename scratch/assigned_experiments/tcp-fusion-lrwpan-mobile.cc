#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/sixlowpan-module.h"

#include <fstream>
#include <iomanip>
#include <set>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpFusionLrWpanMobileAssigned");

int
main(int argc, char* argv[])
{
    std::string tcpVariant = "ns3::TcpFusion";
    uint32_t nNodes = 60;
    uint32_t nFlows = 30;
    uint32_t pktPerSec = 300;
    double speed = 15.0;
    double simTime = 60.0;
    uint32_t seed = 1;
    uint32_t payloadSize = 128;
    double areaSide = 120.0;

    // Analytical energy parameters (J/bit and W)
    double txJPerBit = 50e-9;
    double rxJPerBit = 30e-9;
    double idlePowerW = 0.003;

    std::string outFile = "scratch/assigned_experiments/results/mobile_raw_results.csv";
    bool append = true;

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpVariant", "TCP variant (e.g., ns3::TcpFusion)", tcpVariant);
    cmd.AddValue("nNodes", "Number of mobile nodes", nNodes);
    cmd.AddValue("nFlows", "Number of traffic flows", nFlows);
    cmd.AddValue("pktPerSec", "Packets per second per flow", pktPerSec);
    cmd.AddValue("speed", "Node speed (m/s)", speed);
    cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
    cmd.AddValue("seed", "RNG seed", seed);
    cmd.AddValue("payloadSize", "Packet payload size (bytes)", payloadSize);
    cmd.AddValue("areaSide", "Square area side length (m)", areaSide);
    cmd.AddValue("txJPerBit", "Analytical TX energy per bit (J/bit)", txJPerBit);
    cmd.AddValue("rxJPerBit", "Analytical RX energy per bit (J/bit)", rxJPerBit);
    cmd.AddValue("idlePowerW", "Analytical idle power per node (W)", idlePowerW);
    cmd.AddValue("outFile", "Output CSV file", outFile);
    cmd.AddValue("append", "Append to output CSV", append);
    cmd.Parse(argc, argv);

    nFlows = std::min(nFlows, nNodes > 1 ? nNodes - 1 : 0);

    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(1);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName(tcpVariant)));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(536));

    NodeContainer nodes;
    nodes.Create(nNodes);

    MobilityHelper mobility;
    Ptr<PositionAllocator> positionAlloc = CreateObjectWithAttributes<RandomRectanglePositionAllocator>(
        "X",
        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(areaSide) + "]"),
        "Y",
        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(areaSide) + "]"));

    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                              "Speed",
                              StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(speed) + "]"),
                              "Pause",
                              StringValue("ns3::ConstantRandomVariable[Constant=0.0]"),
                              "PositionAllocator",
                              PointerValue(positionAlloc));
    mobility.Install(nodes);

    LrWpanHelper lrWpan;
    NetDeviceContainer lrDevices = lrWpan.Install(nodes);
    lrWpan.CreateAssociatedPan(lrDevices, 0);

    SixLowPanHelper sixlowpan;
    NetDeviceContainer sixDevices = sixlowpan.Install(lrDevices);

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv6AddressHelper ipv6;
    ipv6.SetBase(Ipv6Address("2001:1::"), Ipv6Prefix(64));
    Ipv6InterfaceContainer ifs = ipv6.Assign(sixDevices);

    for (uint32_t i = 0; i < ifs.GetN(); ++i)
    {
        ifs.SetForwarding(i, true);
    }

    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    pairs.reserve(nFlows);
    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
    std::set<uint64_t> seen;
    while (pairs.size() < nFlows)
    {
        uint32_t s = uv->GetInteger(0, nNodes - 1);
        uint32_t d = uv->GetInteger(0, nNodes - 1);
        if (s == d)
        {
            continue;
        }
        uint64_t key = (static_cast<uint64_t>(s) << 32) | d;
        if (!seen.insert(key).second)
        {
            continue;
        }
        pairs.emplace_back(s, d);
    }

    uint64_t dataRateBps = static_cast<uint64_t>(pktPerSec) * payloadSize * 8;

    for (uint32_t i = 0; i < pairs.size(); ++i)
    {
        uint32_t src = pairs[i].first;
        uint32_t dst = pairs[i].second;
        uint16_t port = 42000 + i;

        PacketSinkHelper sink("ns3::TcpSocketFactory", Inet6SocketAddress(Ipv6Address::GetAny(), port));
        ApplicationContainer sinkApp = sink.Install(nodes.Get(dst));
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simTime));

        OnOffHelper onoff("ns3::TcpSocketFactory", Inet6SocketAddress(ifs.GetAddress(dst, 1), port));
        onoff.SetAttribute("DataRate", DataRateValue(DataRate(dataRateBps)));
        onoff.SetAttribute("PacketSize", UintegerValue(payloadSize));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        ApplicationContainer app = onoff.Install(nodes.Get(src));
        app.Start(Seconds(1.0 + 0.01 * i));
        app.Stop(Seconds(simTime - 0.1));
    }

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();

    uint64_t txPackets = 0;
    uint64_t rxPackets = 0;
    uint64_t txBytes = 0;
    uint64_t rxBytes = 0;
    Time delaySum = Seconds(0);

    for (const auto& kv : monitor->GetFlowStats())
    {
        const auto& st = kv.second;
        txPackets += st.txPackets;
        rxPackets += st.rxPackets;
        txBytes += st.txBytes;
        rxBytes += st.rxBytes;
        delaySum += st.delaySum;
    }

    double throughputMbps = rxBytes * 8.0 / (simTime * 1e6);
    double avgDelayMs = rxPackets > 0 ? (delaySum.GetSeconds() * 1000.0 / rxPackets) : 0.0;
    double pdr = txPackets > 0 ? static_cast<double>(rxPackets) / txPackets : 0.0;
    double dropRatio = txPackets > 0 ? static_cast<double>(txPackets - rxPackets) / txPackets : 0.0;

    double txBits = txBytes * 8.0;
    double rxBits = rxBytes * 8.0;
    double energyJ = txBits * txJPerBit + rxBits * rxJPerBit + nNodes * idlePowerW * simTime;

    std::ofstream out;
    if (append)
    {
        out.open(outFile.c_str(), std::ios::out | std::ios::app);
    }
    else
    {
        out.open(outFile.c_str(), std::ios::out | std::ios::trunc);
    }

    if (!out.good())
    {
        NS_FATAL_ERROR("Failed to open output CSV: " << outFile);
    }

    if (out.tellp() == 0)
    {
        out << "scenario,tcpVariant,nNodes,nFlows,pktPerSec,speed,simTime,seed,throughputMbps,avgDelayMs,pdr,dropRatio,energyJ\n";
    }

    out << std::fixed << std::setprecision(6) << "lrwpan-mobile"
        << "," << tcpVariant << "," << nNodes << "," << nFlows << "," << pktPerSec << "," << speed
        << "," << simTime << "," << seed << "," << throughputMbps << "," << avgDelayMs << "," << pdr
        << "," << dropRatio << "," << energyJ << "\n";

    out.close();

    Simulator::Destroy();
    return 0;
}
