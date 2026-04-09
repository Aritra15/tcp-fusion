/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ssid.h"
#include "ns3/yans-wifi-helper.h"

// Default Network Topology
//
//   Wifi 10.1.3.0
//                 AP
//  *    *    *    *
//  |    |    |    |    10.1.1.0
// n5   n6   n7   n0 -------------- n1   n2   n3   n4
//                   point-to-point  |    |    |    |
//                                   ================
//                                     LAN 10.1.2.0

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ThirdScriptExample");

int
main(int argc, char* argv[])
{
    // Simulation knobs that can be overridden from the command line.
    bool verbose = true;
    uint32_t nCsma = 3;
    uint32_t nWifi = 3;
    bool tracing = false;

    // Register command-line arguments for runtime customization.
    CommandLine cmd(__FILE__);
    cmd.AddValue("nCsma", "Number of \"extra\" CSMA nodes/devices", nCsma);
    cmd.AddValue("nWifi", "Number of wifi STA devices", nWifi);
    cmd.AddValue("verbose", "Tell echo applications to log if true", verbose);
    cmd.AddValue("tracing", "Enable pcap tracing", tracing);

    cmd.Parse(argc, argv);

    // The underlying restriction of 18 is due to the grid position
    // allocator's configuration; the grid layout will exceed the
    // bounding box if more than 18 nodes are provided.
    if (nWifi > 18)
    {
        std::cout << "nWifi should be 18 or less; otherwise grid layout exceeds the bounding box"
                  << std::endl;
        return 1;
    }

    if (verbose)
    {
        // Print client/server app events (packet send/receive logs).
        LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
    }

    // Create the two endpoints of the point-to-point backbone link.
    NodeContainer p2pNodes;
    p2pNodes.Create(2);

    // Configure the point-to-point link characteristics.
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer p2pDevices;
    p2pDevices = pointToPoint.Install(p2pNodes);

    // Build a CSMA LAN off the second p2p node.
    NodeContainer csmaNodes;
    csmaNodes.Add(p2pNodes.Get(1));
    csmaNodes.Create(nCsma);

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));

    NetDeviceContainer csmaDevices;
    csmaDevices = csma.Install(csmaNodes);

    // Create Wi-Fi station nodes and use the first p2p node as the AP.
    // STA = Station: a client-side Wi-Fi node that associates to an AP.
    // AP  = Access Point: infrastructure node that bridges wireless clients
    // to the rest of the network (here, into the p2p + CSMA side).
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(nWifi);
    NodeContainer wifiApNode = p2pNodes.Get(0);

    // Wi-Fi setup in ns-3 is split into parts:
    // 1) Channel helper: propagation/channel behavior,
    // 2) PHY helper: radio-level settings attached to that channel,
    // 3) MAC helper: role-specific MAC type (STA or AP),
    // 4) WifiHelper::Install(): creates NetDevices on target nodes.
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    Ssid ssid = Ssid("ns-3-ssid");

    WifiHelper wifi;

    NetDeviceContainer staDevices;
    // Configure STA MAC: these nodes behave like Wi-Fi clients.
    // ActiveProbing=false means STAs wait for AP beacons instead of sending
    // probe requests; they join the network with matching SSID.
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    staDevices = wifi.Install(phy, mac, wifiStaNodes);

    NetDeviceContainer apDevices;
    // Configure AP MAC: this node advertises the SSID and accepts STA associations.
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    apDevices = wifi.Install(phy, mac, wifiApNode);

    // Mobility model assignment is done in two stages:
    // - SetPositionAllocator: initial coordinates at install time.
    // - SetMobilityModel: how positions change after simulation starts.
    MobilityHelper mobility;

    // GridPositionAllocator places nodes at deterministic grid points:
    // start at (MinX, MinY), then move by DeltaX/DeltaY, with GridWidth=3
    // and RowFirst ordering (fill row cells first, then next row).
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX",
                                  DoubleValue(0.0),
                                  "MinY",
                                  DoubleValue(0.0),
                                  "DeltaX",
                                  DoubleValue(5.0),
                                  "DeltaY",
                                  DoubleValue(10.0),
                                  "GridWidth",
                                  UintegerValue(3),
                                  "LayoutType",
                                  StringValue("RowFirst"));

    // RandomWalk2dMobilityModel: each STA keeps moving with random direction
    // changes inside the given rectangle; when it reaches bounds, motion is
    // constrained by the model/boundary logic.
    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Bounds",
                              RectangleValue(Rectangle(-50, 50, -50, 50)));
    mobility.Install(wifiStaNodes);

    // ConstantPositionMobilityModel: AP position never changes after placement.
    // Difference from RandomWalk2d: static anchor vs continuously moving nodes.
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNode);

    // Install TCP/IP stack on all nodes that need Layer-3 networking.
    InternetStackHelper stack;
    stack.Install(csmaNodes);
    stack.Install(wifiApNode);
    stack.Install(wifiStaNodes);

    // Assign IPv4 subnets to each link segment.
    Ipv4AddressHelper address;

    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer p2pInterfaces;
    p2pInterfaces = address.Assign(p2pDevices);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer csmaInterfaces;
    csmaInterfaces = address.Assign(csmaDevices);

    address.SetBase("10.1.3.0", "255.255.255.0");
    address.Assign(staDevices);
    address.Assign(apDevices);

    // Start a UDP echo server on the last CSMA node.
    UdpEchoServerHelper echoServer(9);

    ApplicationContainer serverApps = echoServer.Install(csmaNodes.Get(nCsma));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(10.0));

    // Start a UDP echo client on the last Wi-Fi station.
    // It sends one 1024-byte packet to the server at t=2s.
    UdpEchoClientHelper echoClient(csmaInterfaces.GetAddress(nCsma), 9);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = echoClient.Install(wifiStaNodes.Get(nWifi - 1));
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(10.0));

    // Compute global IPv4 routes for all nodes that have IPv4 + routing enabled
    // at this point in the simulation, not only the client/server endpoints.
    // In this topology, it installs paths so Wi-Fi subnet can reach CSMA subnet
    // through AP node (n0) and p2p peer (n1).
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // End simulation at 10 seconds.
    Simulator::Stop(Seconds(10.0));

    if (tracing)
    {
        // Optionally generate pcap traces for Wi-Fi, p2p, and CSMA devices.
        phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
        pointToPoint.EnablePcapAll("third");
        phy.EnablePcap("third", apDevices.Get(0));
        csma.EnablePcap("third", csmaDevices.Get(0), true);
    }

    // Packet flow for this setup (forward path):
    // last Wi-Fi STA -> AP (n0) -> p2p link (n0<->n1) -> CSMA LAN -> server node.
    // Echo reply follows reverse path back to the same STA.
    // Run the event scheduler and free simulator resources.
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
