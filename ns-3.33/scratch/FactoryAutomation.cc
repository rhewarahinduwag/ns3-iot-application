#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-global-routing-helper.h"

// including application module for the inheritance
#include "ns3/applications-module.h"

// including NetAnim (Network Animator) to the program
#include "ns3/netanim-module.h"

// Proposed Network Topology 
//
//       10.1.1.0
// n0 -------------- n1   n2   n3   n4
//    point-to-point  |    |    |    |
//                    ================
//                      LAN 10.1.2.0


using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FactoryAutomation");

int 
main (int argc, char *argv[])
{
  bool verbose = true;
  uint32_t nCsma = 3; // unsigned integer 32 bits
  
// nCsma nodes are n2, n3, n4 and n1 is a part of p2p node
  CommandLine cmd (__FILE__);
  cmd.AddValue ("nCsma", "Number of \"extra\" CSMA nodes/devices", nCsma);
  cmd.AddValue ("verbose", "Tell echo applications to log if true", verbose);

  cmd.Parse (argc,argv);

  if (verbose)
    {
      LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
    }

  nCsma = nCsma == 0 ? 1 : nCsma;

  NodeContainer p2pNodes;
  p2pNodes.Create (2); // creating 2 p2p nodes (n0 and n1)

  NodeContainer csmaNodes;
  csmaNodes.Add (p2pNodes.Get (1)); // this is n1
  csmaNodes.Create (nCsma); // nCsma = 3

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("1Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2.5ms"));

  NetDeviceContainer p2pDevices;
  p2pDevices = pointToPoint.Install (p2pNodes);

  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", StringValue ("100Mbps"));
  csma.SetChannelAttribute ("Delay", TimeValue (NanoSeconds (6560)));

  NetDeviceContainer csmaDevices;
  csmaDevices = csma.Install (csmaNodes);

  InternetStackHelper stack;
  stack.Install (p2pNodes.Get (0)); // installed n0
  stack.Install (csmaNodes); // installed n1, n2, n3, and n4

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer p2pInterfaces;
  p2pInterfaces = address.Assign (p2pDevices);

  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer csmaInterfaces;
  csmaInterfaces = address.Assign (csmaDevices);

  UdpEchoServerHelper echoServer (9);

  ApplicationContainer serverApps = echoServer.Install (csmaNodes.Get (nCsma));
  // the server is csmaNodes.get(3) ----> n4 is the server as nCsma = 3
  serverApps.Start (Seconds (1.0));
  serverApps.Stop (Seconds (10.0));

  UdpEchoClientHelper echoClient (csmaInterfaces.GetAddress (nCsma), 9);
  echoClient.SetAttribute ("MaxPackets", UintegerValue (1));
  echoClient.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
  echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

  ApplicationContainer clientApps = echoClient.Install (p2pNodes.Get (0)); // n0 is the client
  clientApps.Start (Seconds (2.0));
  clientApps.Stop (Seconds (10.0));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  pointToPoint.EnablePcapAll ("second");
  csma.EnablePcap ("second", csmaDevices.Get (1), true); // n1
  
  // Network Animation
  AnimationInterface anim("FactoryAutomation.xml");
  anim.SetConstantPosition(p2pNodes.Get(0),10.0,10.0);
  anim.SetConstantPosition(p2pNodes.Get(0),20.0,20.0);
  anim.SetConstantPosition(csmaNodes.Get(1),30.0,30.0);
  anim.SetConstantPosition(csmaNodes.Get(2),40.0,40.0);
  anim.SetConstantPosition(csmaNodes.Get(3),50.0,50.0);

  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
