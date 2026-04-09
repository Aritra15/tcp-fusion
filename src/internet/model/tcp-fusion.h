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

#ifndef TCP_FUSION_H
#define TCP_FUSION_H

#include "tcp-congestion-ops.h"

#include "ns3/data-rate.h"
#include "ns3/traced-value.h"

namespace ns3
{

class TcpSocketState;

/**
 * \ingroup congestionOps
 *
 * \brief An implementation of TCP-Fusion congestion control.
 *
 * TCP-Fusion is a hybrid congestion control that combines:
 *   - TCP-Vegas style delay-based three-phase congestion avoidance
 *   - TCP-Westwood style bandwidth estimation for smart window reduction
 *   - TCP-Reno AIMD as a safety-net floor
 *
 * The algorithm maintains two windows: a "fusion" window (delay-aware) and
 * a "reno" window (standard AIMD). The actual cwnd is always
 * max(fusion_cwnd, reno_cwnd), guaranteeing at least Reno-level performance.
 *
 * Congestion Avoidance (per RTT):
 *   diff = cwnd * (RTT - RTTmin) / RTT
 *   alpha = cwnd * Dmin / RTT
 *   if diff < alpha:       cwnd += Winc / cwnd      (increase phase)
 *   else if diff > 3*alpha: cwnd += (-diff+alpha)/cwnd (decrease phase)
 *   else:                   cwnd unchanged            (steady phase)
 *
 * Window Reduction on Loss:
 *   cwnd_new = max(RTTmin/RTT * cwnd, cwnd/2)
 *
 * Bandwidth estimation (Westwood-style) is used to compute Winc = BW / beta.
 */
class TcpFusion : public TcpNewReno
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    TcpFusion();

    /**
     * \brief Copy constructor
     * \param sock the object to copy
     */
    TcpFusion(const TcpFusion& sock);
    ~TcpFusion() override;

    std::string GetName() const override;

    void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) override;

    void CongestionStateSet(Ptr<TcpSocketState> tcb,
                            const TcpSocketState::TcpCongState_t newState) override;

    void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;

    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;

    Ptr<TcpCongestionOps> Fork() override;

  private:
    /**
     * \brief Enable Fusion mode (called on CA_OPEN)
     */
    void EnableFusion(Ptr<TcpSocketState> tcb);

    /**
     * \brief Disable Fusion mode
     */
    void DisableFusion();

    /**
     * \brief Estimate bandwidth using Westwood-style sampling
     * \param rtt last RTT
     * \param tcb socket state
     */
    void EstimateBW(const Time& rtt, Ptr<TcpSocketState> tcb);

    // --- Vegas-style delay parameters ---
    Time m_baseRtt;   //!< Minimum RTT ever observed (propagation delay)
    Time m_minRtt;    //!< Minimum RTT in the current RTT window
    Time m_currentRtt;//!< Current (latest) RTT sample
    uint32_t m_cntRtt;//!< Number of RTT samples in this window

    // --- Fusion operating state ---
    bool m_doingFusionNow;        //!< Whether we are in Fusion mode
    SequenceNumber32 m_begSndNxt; //!< Right edge at start of current Vegas cycle

    // --- Reno shadow window (in bytes) ---
    uint32_t m_renoCwnd; //!< Shadow Reno window for the safety-net floor

    // --- Bandwidth estimation (Westwood-style) ---
    TracedValue<DataRate> m_currentBW; //!< Estimated bandwidth
    DataRate m_lastSampleBW;           //!< Last BW sample
    DataRate m_lastBW;                 //!< Last BW after Tustin filter
    uint32_t m_ackedSegments;          //!< Segments ACKed in current estimation window
    bool m_isCount;                    //!< Whether we started counting for BW estimate
    EventId m_bwEstimateEvent;         //!< Scheduled BW estimation event

    // --- Tunable parameters ---
    double m_dMinAlpha;  //!< Dmin for alpha calculation (seconds), default tcp_tick ~ 0.004s
    double m_dMinWinc;   //!< Dmin for Winc calculation (seconds), default 0.001s
    double m_beta;       //!< Divisor for Winc = BW / beta (bps), default 12e6
};

} // namespace ns3

#endif // TCP_FUSION_H
