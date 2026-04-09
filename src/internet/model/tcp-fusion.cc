/*
 * TCP-Fusion: A Hybrid Congestion Control Algorithm for High-speed Networks
 *
 * Based on:
 *   K. Kaneko, T. Fujikawa, Z. Su, J. Katto,
 *   "TCP-Fusion: A Hybrid Congestion Control Algorithm for High-speed Networks"
 *   Waseda University
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 */

#include "tcp-fusion.h"

#include "tcp-socket-state.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TcpFusion");
NS_OBJECT_ENSURE_REGISTERED(TcpFusion);

TypeId
TcpFusion::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::TcpFusion")
            .SetParent<TcpNewReno>()
            .AddConstructor<TcpFusion>()
            .SetGroupName("Internet")
            .AddAttribute("DminAlpha",
                          "Minimum queuing delay for alpha calculation (seconds). "
                          "Typically one tcp_tick in simulation (~4ms).",
                          DoubleValue(0.004),
                          MakeDoubleAccessor(&TcpFusion::m_dMinAlpha),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("DminWinc",
                          "Minimum queuing delay for Winc calculation (seconds). "
                          "Typically 1ms even on high-speed networks.",
                          DoubleValue(0.001),
                          MakeDoubleAccessor(&TcpFusion::m_dMinWinc),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("Beta",
                          "Divisor for Winc = BW / Beta (in bps). Default 12 Mbps.",
                          DoubleValue(12.0e6),
                          MakeDoubleAccessor(&TcpFusion::m_beta),
                          MakeDoubleChecker<double>(1.0))
            .AddTraceSource("EstimatedBW",
                            "The estimated bandwidth",
                            MakeTraceSourceAccessor(&TcpFusion::m_currentBW),
                            "ns3::TracedValueCallback::DataRate");
    return tid;
}

TcpFusion::TcpFusion()
    : TcpNewReno(),
      m_baseRtt(Time::Max()),
      m_minRtt(Time::Max()),
      m_currentRtt(Time::Max()),
      m_cntRtt(0),
      m_doingFusionNow(true),
      m_begSndNxt(0),
      m_renoCwnd(0),
      m_currentBW(0),
      m_lastSampleBW(0),
      m_lastBW(0),
      m_ackedSegments(0),
      m_isCount(false),
      m_dMinAlpha(0.004),
      m_dMinWinc(0.001),
      m_beta(12.0e6)
{
    NS_LOG_FUNCTION(this);
}

TcpFusion::TcpFusion(const TcpFusion& sock)
    : TcpNewReno(sock),
      m_baseRtt(sock.m_baseRtt),
      m_minRtt(sock.m_minRtt),
      m_currentRtt(sock.m_currentRtt),
      m_cntRtt(sock.m_cntRtt),
      m_doingFusionNow(true),
      m_begSndNxt(0),
      m_renoCwnd(sock.m_renoCwnd),
      m_currentBW(sock.m_currentBW),
      m_lastSampleBW(sock.m_lastSampleBW),
      m_lastBW(sock.m_lastBW),
      m_ackedSegments(0),
      m_isCount(false),
      m_dMinAlpha(sock.m_dMinAlpha),
      m_dMinWinc(sock.m_dMinWinc),
      m_beta(sock.m_beta)
{
    NS_LOG_FUNCTION(this);
}

TcpFusion::~TcpFusion()
{
    NS_LOG_FUNCTION(this);
}

Ptr<TcpCongestionOps>
TcpFusion::Fork()
{
    return CopyObject<TcpFusion>(this);
}

std::string
TcpFusion::GetName() const
{
    return "TcpFusion";
}

// ---------------------------------------------------------------------------
// PktsAcked: Called on every ACK. Track RTT samples + bandwidth estimation.
// ---------------------------------------------------------------------------
void
TcpFusion::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked << rtt);

    if (rtt.IsZero())
    {
        return;
    }

    // --- RTT tracking (Vegas-style) ---
    m_minRtt = std::min(m_minRtt, rtt);
    m_baseRtt = std::min(m_baseRtt, rtt);
    m_currentRtt = rtt;
    m_cntRtt++;

    NS_LOG_DEBUG("PktsAcked: rtt=" << rtt.GetSeconds()
                 << " baseRtt=" << m_baseRtt.GetSeconds()
                 << " minRtt=" << m_minRtt.GetSeconds()
                 << " cntRtt=" << m_cntRtt);

    // --- Bandwidth estimation (Westwood+-style, once per RTT) ---
    m_ackedSegments += segmentsAcked;

    if (!m_isCount)
    {
        m_isCount = true;
        m_bwEstimateEvent.Cancel();
        m_bwEstimateEvent =
            Simulator::Schedule(rtt, &TcpFusion::EstimateBW, this, rtt, tcb);
    }
}

// ---------------------------------------------------------------------------
// EstimateBW: Westwood+-style bandwidth estimation with Tustin filter
// ---------------------------------------------------------------------------
void
TcpFusion::EstimateBW(const Time& rtt, Ptr<TcpSocketState> tcb)
{
    NS_LOG_FUNCTION(this);

    if (rtt.IsZero())
    {
        return;
    }

    m_currentBW = DataRate(m_ackedSegments * tcb->m_segmentSize * 8.0 / rtt.GetSeconds());
    m_isCount = false;
    m_ackedSegments = 0;

    // Tustin low-pass filter (alpha = 0.9)
    constexpr double ALPHA = 0.9;
    DataRate sampleBW = m_currentBW;
    m_currentBW =
        (m_lastBW * ALPHA) + (((sampleBW + m_lastSampleBW) * 0.5) * (1 - ALPHA));
    m_lastSampleBW = sampleBW;
    m_lastBW = m_currentBW;

    NS_LOG_DEBUG("EstimateBW: " << m_currentBW);
}

// ---------------------------------------------------------------------------
// CongestionStateSet: Enable/disable Fusion on congestion state changes
// ---------------------------------------------------------------------------
void
TcpFusion::CongestionStateSet(Ptr<TcpSocketState> tcb,
                               const TcpSocketState::TcpCongState_t newState)
{
    NS_LOG_FUNCTION(this << tcb << newState);

    if (newState == TcpSocketState::CA_OPEN)
    {
        EnableFusion(tcb);
    }
    else
    {
        DisableFusion();
    }
}

void
TcpFusion::EnableFusion(Ptr<TcpSocketState> tcb)
{
    NS_LOG_FUNCTION(this << tcb);
    m_doingFusionNow = true;
    m_begSndNxt = tcb->m_nextTxSequence;
    m_cntRtt = 0;
    m_minRtt = Time::Max();
}

void
TcpFusion::DisableFusion()
{
    NS_LOG_FUNCTION(this);
    m_doingFusionNow = false;
}

// ---------------------------------------------------------------------------
// IncreaseWindow: The core TCP-Fusion congestion avoidance logic.
//
// Three-phase delay-based increase (Vegas-inspired) with:
//   - Adaptive alpha = cwnd * Dmin / RTT
//   - Winc = BW / beta (from Westwood bandwidth estimation)
//   - Reno shadow-window as a performance floor
// ---------------------------------------------------------------------------
void
TcpFusion::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    // If Fusion is not active (e.g., in recovery), fall back to NewReno
    if (!m_doingFusionNow)
    {
        NS_LOG_LOGIC("Fusion not active, using NewReno.");
        TcpNewReno::IncreaseWindow(tcb, segmentsAcked);
        return;
    }

    // --- Slow-start phase: use standard NewReno slow-start ---
    if (tcb->m_cWnd < tcb->m_ssThresh)
    {
        NS_LOG_LOGIC("In slow start.");
        TcpNewReno::SlowStart(tcb, segmentsAcked);

        // Keep reno shadow in sync during slow-start
        m_renoCwnd = tcb->m_cWnd;
        return;
    }

    // --- Congestion avoidance: Fusion logic, once per RTT ---
    // Check if a full RTT has elapsed (Vegas cycle boundary)
    if (tcb->m_lastAckedSeq >= m_begSndNxt)
    {
        NS_LOG_LOGIC("Fusion cycle complete, adjusting window.");

        // Mark the new cycle boundary
        m_begSndNxt = tcb->m_nextTxSequence;

        // Update the Reno shadow window: +1 MSS per RTT (standard AIMD increase)
        m_renoCwnd += tcb->m_segmentSize;

        // Need enough RTT samples to make delay-based decisions
        if (m_cntRtt <= 2)
        {
            NS_LOG_LOGIC("Not enough RTT samples, behaving like NewReno.");
            TcpNewReno::CongestionAvoidance(tcb, segmentsAcked);
            // Keep reno shadow in sync
            m_renoCwnd = std::max(m_renoCwnd, tcb->m_cWnd.Get());
        }
        else
        {
            uint32_t segCwnd = tcb->GetCwndInSegments();
            double rttSec = m_minRtt.GetSeconds();

            // Guard against invalid RTT
            if (rttSec <= 0.0 || m_baseRtt.IsZero() || m_baseRtt == Time::Max())
            {
                NS_LOG_LOGIC("Invalid RTT, using NewReno.");
                TcpNewReno::CongestionAvoidance(tcb, segmentsAcked);
                m_renoCwnd = std::max(m_renoCwnd, tcb->m_cWnd.Get());
                m_cntRtt = 0;
                m_minRtt = Time::Max();
                return;
            }

            double baseRttSec = m_baseRtt.GetSeconds();

            // ---- Equation: diff = cwnd * (RTT - RTTmin) / RTT ----
            // Using minRtt (per-window minimum) and baseRtt (global minimum)
            double diff = (double)segCwnd * (rttSec - baseRttSec) / rttSec;
            if (diff < 0.0)
            {
                diff = 0.0;
            }

            // ---- Equation: alpha = cwnd * Dmin / RTT ----
            double alpha = (double)segCwnd * m_dMinAlpha / rttSec;
            if (alpha < 1.0)
            {
                alpha = 1.0; // At least 1 segment to allow phase switching
            }

            // ---- Compute Winc = BW / beta ----
            double bwBps = m_currentBW.Get().GetBitRate();
            double winc = bwBps / m_beta;
            if (winc < 1.0)
            {
                winc = 1.0; // At least 1 segment increment
            }

            NS_LOG_DEBUG("Fusion CA: segCwnd=" << segCwnd
                         << " diff=" << diff
                         << " alpha=" << alpha
                         << " 3*alpha=" << 3.0 * alpha
                         << " winc=" << winc
                         << " BW=" << bwBps);

            // ---- Three-phase window adjustment ----
            double newSegCwnd = (double)segCwnd;

            if (diff < alpha)
            {
                // INCREASE PHASE: link underutilized
                newSegCwnd = (double)segCwnd + winc / (double)segCwnd;
                NS_LOG_LOGIC("Fusion INCREASE phase: +" << winc / (double)segCwnd);
            }
            else if (diff > 3.0 * alpha)
            {
                // DECREASE PHASE: early congestion detected
                double decrement = (diff - alpha) / (double)segCwnd;
                newSegCwnd = (double)segCwnd - decrement;
                NS_LOG_LOGIC("Fusion DECREASE phase: -" << decrement);
            }
            else
            {
                // STEADY PHASE: good balance
                NS_LOG_LOGIC("Fusion STEADY phase.");
            }

            // Convert back to bytes
            uint32_t fusionCwnd;
            if (newSegCwnd < 2.0)
            {
                fusionCwnd = 2 * tcb->m_segmentSize;
            }
            else
            {
                fusionCwnd = static_cast<uint32_t>(newSegCwnd) * tcb->m_segmentSize;
            }

            // ---- RENO FLOOR: cwnd = max(fusion_cwnd, reno_cwnd) ----
            tcb->m_cWnd = std::max(fusionCwnd, m_renoCwnd);

            NS_LOG_DEBUG("Fusion cwnd=" << fusionCwnd
                         << " renoCwnd=" << m_renoCwnd
                         << " final cwnd=" << tcb->m_cWnd.Get());
        }

        // Reset per-RTT counters
        m_cntRtt = 0;
        m_minRtt = Time::Max();
    }
    else
    {
        // Not yet at the Vegas cycle boundary — do nothing in CA
        // (window is adjusted once per RTT, not per ACK)
    }
}

// ---------------------------------------------------------------------------
// GetSsThresh: Window reduction on loss.
//
// TCP-Fusion uses: cwnd_new = max(RTTmin/RTT * cwnd, cwnd/2)
// This is the Westwood-RE reduction capped at standard halving.
// Also resets the Reno shadow window to the new ssthresh.
// ---------------------------------------------------------------------------
uint32_t
TcpFusion::GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
    NS_LOG_FUNCTION(this << tcb << bytesInFlight);

    uint32_t cwnd = tcb->m_cWnd.Get();

    // Standard halving (Reno-style floor)
    uint32_t halfCwnd = std::max(cwnd / 2, 2 * tcb->m_segmentSize);

    // Westwood-RE style: RTTmin/RTT * cwnd
    uint32_t westwoodCwnd = halfCwnd; // default to half
    if (m_baseRtt != Time::Max() && m_currentRtt != Time::Max() &&
        !m_baseRtt.IsZero() && !m_currentRtt.IsZero())
    {
        double ratio = m_baseRtt.GetSeconds() / m_currentRtt.GetSeconds();
        // Clamp ratio so reduction is at most halving (ratio >= 0.5)
        ratio = std::max(ratio, 0.5);
        westwoodCwnd = static_cast<uint32_t>(ratio * cwnd);
        westwoodCwnd = std::max(westwoodCwnd, 2 * tcb->m_segmentSize);
    }

    // TCP-Fusion: max of the two (least aggressive)
    uint32_t ssThresh = std::max(halfCwnd, westwoodCwnd);

    // Reset the Reno shadow window to the new ssthresh
    m_renoCwnd = ssThresh;

    NS_LOG_DEBUG("GetSsThresh: cwnd=" << cwnd
                 << " halfCwnd=" << halfCwnd
                 << " westwoodCwnd=" << westwoodCwnd
                 << " ssThresh=" << ssThresh);

    return ssThresh;
}

} // namespace ns3
