#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/packet-socket-address.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/data-rate.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "smartgrid-application.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/boolean.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SmartGridApplication");

NS_OBJECT_ENSURE_REGISTERED (SmartGridApplication);

TypeId
SmartGridApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SmartGridApplication")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<SmartGridApplication> ()
    .AddAttribute ("DataRate", "The data rate in on state.",
                   DataRateValue (DataRate ("0kb/s")),
                   MakeDataRateAccessor (&SmartGridApplication::m_cbrRate),
                   MakeDataRateChecker ())
    .AddAttribute ("PacketSize", "The size of packets sent in on state",
                   UintegerValue (0),
                   MakeUintegerAccessor (&SmartGridApplication::m_pktSize),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("Remote", "The address of the destination",
                   AddressValue (),
                   MakeAddressAccessor (&SmartGridApplication::m_peer),
                   MakeAddressChecker ())
    .AddAttribute ("Local",
                   "The Address on which to bind the socket. If not set, it is generated automatically.",
                   AddressValue (),
                   MakeAddressAccessor (&SmartGridApplication::m_local),
                   MakeAddressChecker ())
    .AddAttribute ("MaxBytes", 
                   "The total number of bytes to send.",
                   UintegerValue (0),
                   MakeUintegerAccessor (&SmartGridApplication::m_maxBytes),
                   MakeUintegerChecker<uint64_t> ())
    .AddAttribute ("Protocol", "The type of protocol to use. This should be "
                   "a subclass of ns3::SocketFactory",
                   TypeIdValue (UdpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&SmartGridApplication::m_tid),
                   // This should check for SocketFactory as a parent
                   MakeTypeIdChecker ())
    .AddAttribute ("EnableSeqTsSizeHeader",
                   "Enable use of SeqTsSizeHeader for sequence number and timestamp",
                   BooleanValue (false),
                   MakeBooleanAccessor (&SmartGridApplication::m_enableSeqTsSizeHeader),
                   MakeBooleanChecker ())
    .AddTraceSource ("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor (&SmartGridApplication::m_txTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("TxWithAddresses", "A new packet is created and is sent",
                     MakeTraceSourceAccessor (&SmartGridApplication::m_txTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
    .AddTraceSource ("TxWithSeqTsSize", "A new packet is created with SeqTsSizeHeader",
                     MakeTraceSourceAccessor (&SmartGridApplication::m_txTraceWithSeqTsSize),
                     "ns3::PacketSink::SeqTsSizeCallback")
  ;
  return tid;
}


SmartGridApplication::SmartGridApplication ()
  : m_socket (0),
    m_connected (false),
    m_residualBits (0),
    m_lastStartTime (Seconds (0)),
    m_interArrivalTime(Seconds (0)),
    m_totBytes (0),
    m_unsentPacket (0)
{
  NS_LOG_FUNCTION (this);
}

SmartGridApplication::~SmartGridApplication()
{
  NS_LOG_FUNCTION (this);
}

void 
SmartGridApplication::SetMaxBytes (uint64_t maxBytes)
{
  NS_LOG_FUNCTION (this << maxBytes);
  m_maxBytes = maxBytes;
}

void 
SmartGridApplication::SetPacketSize (uint32_t pktSize)
{
  NS_LOG_FUNCTION (this << pktSize);
  m_pktSize = pktSize;
}

void 
SmartGridApplication::SetInterArrivalTime (Time interArrivalTime)
{
  NS_LOG_FUNCTION (this << interArrivalTime);
  m_interArrivalTime = interArrivalTime;
}

void 
SmartGridApplication::SetDataRate (DataRate cbrRate)
{
  NS_LOG_FUNCTION (this << cbrRate);
  m_cbrRate = cbrRate;
}

Ptr<Socket>
SmartGridApplication::GetSocket (void) const
{
  NS_LOG_FUNCTION (this);
  return m_socket;
}

void
SmartGridApplication::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  CancelEvents ();
  m_socket = 0;
  m_unsentPacket = 0;
  // chain up
  Application::DoDispose ();
}

// Application Methods
void SmartGridApplication::StartApplication () // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);

  // Create the socket if not already
  if (!m_socket)
    {
      m_socket = Socket::CreateSocket (GetNode (), m_tid);
      int ret = -1;

      if (! m_local.IsInvalid())
        {
          NS_ABORT_MSG_IF ((Inet6SocketAddress::IsMatchingType (m_peer) && InetSocketAddress::IsMatchingType (m_local)) ||
                           (InetSocketAddress::IsMatchingType (m_peer) && Inet6SocketAddress::IsMatchingType (m_local)),
                           "Incompatible peer and local address IP version");
          ret = m_socket->Bind (m_local);
        }
      else
        {
          if (Inet6SocketAddress::IsMatchingType (m_peer))
            {
              ret = m_socket->Bind6 ();
            }
          else if (InetSocketAddress::IsMatchingType (m_peer) ||
                   PacketSocketAddress::IsMatchingType (m_peer))
            {
              ret = m_socket->Bind ();
            }
        }

      if (ret == -1)
        {
          NS_FATAL_ERROR ("Failed to bind socket");
        }

      m_socket->Connect (m_peer);
      m_socket->SetAllowBroadcast (true);
      m_socket->ShutdownRecv ();

      m_socket->SetConnectCallback (
        MakeCallback (&SmartGridApplication::ConnectionSucceeded, this),
        MakeCallback (&SmartGridApplication::ConnectionFailed, this));
    }
  m_cbrRateFailSafe = m_cbrRate;

  // Insure no pending event
  CancelEvents ();
  // If we are not yet connected, there is nothing to do here
  // The ConnectionComplete upcall will start timers at that time
  //if (!m_connected) return;
  
}

void SmartGridApplication::StopApplication () // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);

  CancelEvents ();
  if(m_socket != 0)
    {
      m_socket->Close ();
    }
  else
    {
      NS_LOG_WARN ("SmartGridApplication found null socket to close in StopApplication");
    }
}

void SmartGridApplication::CancelEvents ()
{
  NS_LOG_FUNCTION (this);

  if (m_sendEvent.IsRunning () && m_cbrRateFailSafe == m_cbrRate )
    { // Cancel the pending send packet event
      // Calculate residual bits since last packet sent
      Time delta (Simulator::Now () - m_lastStartTime);
      int64x64_t bits = delta.To (Time::S) * m_cbrRate.GetBitRate ();
      m_residualBits += bits.GetHigh ();
    }
  m_cbrRateFailSafe = m_cbrRate;
  Simulator::Cancel (m_sendEvent);
  Simulator::Cancel (m_startStopEvent);
  // Canceling events may cause discontinuity in sequence number if the
  // SeqTsSizeHeader is header, and m_unsentPacket is true
  if (m_unsentPacket)
    {
      NS_LOG_DEBUG ("Discarding cached packet upon CancelEvents ()");
    }
  m_unsentPacket = 0;
}

// Private helpers
void SmartGridApplication::ScheduleNextTx ()
{
  NS_LOG_FUNCTION (this);

  if (m_maxBytes == 0 || m_totBytes < m_maxBytes)
    {
      NS_ABORT_MSG_IF (m_residualBits > m_pktSize * 8, "Calculation to compute next send time will overflow");
      uint32_t bits = m_pktSize * 8 - m_residualBits;
      NS_LOG_LOGIC ("bits = " << bits);
      // packet distribution
      if(m_interArrivalTime == Seconds(0))
      {
      Time nextTime (Seconds (bits /
                              static_cast<double>(m_cbrRate.GetBitRate ()))); // Time till next packet
      NS_LOG_LOGIC ("nextTime = " << nextTime.As (Time::S));
      m_sendEvent = Simulator::Schedule (nextTime,
                                         &SmartGridApplication::SendPacket, this);
      }else{
      Time nextTime (m_interArrivalTime); // Time till next packet
      NS_LOG_LOGIC ("nextTime = " << nextTime.As (Time::S));
      m_sendEvent = Simulator::Schedule (nextTime,
                                         &SmartGridApplication::SendPacket, this);
      }
      
    }
  else
    { // All done, cancel any pending events
      StopApplication ();
    }
}

void SmartGridApplication::SendPacket ()
{
  NS_LOG_FUNCTION (this);

  NS_ASSERT (m_sendEvent.IsExpired ());

  Ptr<Packet> packet;
  if (m_unsentPacket)
    {
      packet = m_unsentPacket;
    }
  else if (m_enableSeqTsSizeHeader)
    {
      Address from, to;
      m_socket->GetSockName (from);
      m_socket->GetPeerName (to);
      SeqTsSizeHeader header;
      header.SetSeq (m_seq++);
      header.SetSize (m_pktSize);
      NS_ABORT_IF (m_pktSize < header.GetSerializedSize ());
      packet = Create<Packet> (m_pktSize - header.GetSerializedSize ());
      // Trace before adding header, for consistency with PacketSink
      m_txTraceWithSeqTsSize (packet, from, to, header);
      packet->AddHeader (header);
    }
  else
    {
      packet = Create<Packet> (m_pktSize);
    }

  int actual = m_socket->Send (packet);
  if ((unsigned) actual == m_pktSize)
    {
      m_txTrace (packet);
      m_totBytes += m_pktSize;
      m_unsentPacket = 0;
      Address localAddress;
      m_socket->GetSockName (localAddress);
      if (InetSocketAddress::IsMatchingType (m_peer))
        {
          NS_LOG_INFO ("At time " << Simulator::Now ().As (Time::S)
                       << " smart-grid application sent "
                       <<  packet->GetSize () << " bytes to "
                       << InetSocketAddress::ConvertFrom(m_peer).GetIpv4 ()
                       << " port " << InetSocketAddress::ConvertFrom (m_peer).GetPort ()
                       << " total Tx " << m_totBytes << " bytes");
          m_txTraceWithAddresses (packet, localAddress, InetSocketAddress::ConvertFrom (m_peer));
        }
      else if (Inet6SocketAddress::IsMatchingType (m_peer))
        {
          NS_LOG_INFO ("At time " << Simulator::Now ().As (Time::S)
                       << " smart-grid application sent "
                       <<  packet->GetSize () << " bytes to "
                       << Inet6SocketAddress::ConvertFrom(m_peer).GetIpv6 ()
                       << " port " << Inet6SocketAddress::ConvertFrom (m_peer).GetPort ()
                       << " total Tx " << m_totBytes << " bytes");
          m_txTraceWithAddresses (packet, localAddress, Inet6SocketAddress::ConvertFrom(m_peer));
        }
    }
  else
    {
      NS_LOG_DEBUG ("Unable to send packet; actual " << actual << " size " << m_pktSize << "; caching for later attempt");
      m_unsentPacket = packet;
    }
  m_residualBits = 0;
  m_lastStartTime = Simulator::Now ();
  ScheduleNextTx ();
}


void SmartGridApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  m_connected = true;
}

void SmartGridApplication::ConnectionFailed (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  NS_FATAL_ERROR ("Can't connect");
}


} // Namespace ns3
