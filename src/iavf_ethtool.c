/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013-2024 Intel Corporation */

/* ethtool support for iavf */
#include "iavf.h"

#ifdef SIOCETHTOOL
#include <linux/uaccess.h>

#ifndef ETH_GSTRING_LEN
#define ETH_GSTRING_LEN 32
#endif

#ifdef ETHTOOL_OPS_COMPAT
#include "kcompat_ethtool.c"
#endif

/* ethtool statistics helpers */

/**
 * struct iavf_stats - definition for an ethtool statistic
 * @stat_string: statistic name to display in ethtool -S output
 * @sizeof_stat: the sizeof() the stat, must be no greater than sizeof(u64)
 * @stat_offset: offsetof() the stat from a base pointer
 *
 * This structure defines a statistic to be added to the ethtool stats buffer.
 * It defines a statistic as offset from a common base pointer. Stats should
 * be defined in constant arrays using the IAVF_STAT macro, with every element
 * of the array using the same _type for calculating the sizeof_stat and
 * stat_offset.
 *
 * The @sizeof_stat is expected to be sizeof(u8), sizeof(u16), sizeof(u32) or
 * sizeof(u64). Other sizes are not expected and will produce a WARN_ONCE from
 * the iavf_add_ethtool_stat() helper function.
 *
 * The @stat_string is interpreted as a format string, allowing formatted
 * values to be inserted while looping over multiple structures for a given
 * statistics array. Thus, every statistic string in an array should have the
 * same type and number of format specifiers, to be formatted by variadic
 * arguments to the iavf_add_stat_string() helper function.
 **/
struct iavf_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

/* Helper macro to define an iavf_stat structure with proper size and type.
 * Use this when defining constant statistics arrays. Note that @_type expects
 * only a type name and is used multiple times.
 */
#define IAVF_STAT(_type, _name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = sizeof_field(_type, _stat), \
	.stat_offset = offsetof(_type, _stat) \
}

/* Helper macro for defining some statistics related to queues */
#define IAVF_QUEUE_STAT(_name, _stat) \
	IAVF_STAT(struct iavf_ring, _name, _stat)

/* Stats associated with a Tx or Rx ring */
static const struct iavf_stats iavf_gstrings_queue_stats[] = {
	IAVF_QUEUE_STAT("%s-%u.packets", stats.packets),
	IAVF_QUEUE_STAT("%s-%u.bytes", stats.bytes),
};

#define IAVF_VECTOR_STAT(_name, _stat) \
	IAVF_STAT(struct iavf_q_vector, _name, _stat)

/* Stats associated with a Tx or Rx ring */
static struct iavf_stats iavf_gstrings_queue_stats_poll[] = {
	IAVF_QUEUE_STAT("%s-%u.pkt_busy_poll", ch_q_stats.poll.pkt_busy_poll),
	IAVF_QUEUE_STAT("%s-%u.pkt_not_busy_poll",
			ch_q_stats.poll.pkt_not_busy_poll),
};

static struct iavf_stats iavf_gstrings_queue_stats_tx[] = {
};

static struct iavf_stats iavf_gstrings_queue_stats_rx[] = {
	IAVF_QUEUE_STAT("%s-%u.tcp_ctrl_pkts", ch_q_stats.rx.tcp_ctrl_pkts),
	IAVF_QUEUE_STAT("%s-%u.only_ctrl_pkts", ch_q_stats.rx.only_ctrl_pkts),
	IAVF_QUEUE_STAT("%s-%u.tcp_fin_recv", ch_q_stats.rx.tcp_fin_recv),
	IAVF_QUEUE_STAT("%s-%u.tcp_rst_recv", ch_q_stats.rx.tcp_rst_recv),
	IAVF_QUEUE_STAT("%s-%u.tcp_syn_recv", ch_q_stats.rx.tcp_syn_recv),
	IAVF_QUEUE_STAT("%s-%u.bp_no_data_pkt", ch_q_stats.rx.bp_no_data_pkt),
};

static struct iavf_stats iavf_gstrings_queue_stats_vector[] = {
	/* tracking BP, INT, BP->INT, INT->BP */
	IAVF_VECTOR_STAT("%s-%u.in_bp", ch_stats.in_bp),
	IAVF_VECTOR_STAT("%s-%u.intr_to_bp", ch_stats.intr_to_bp),
	IAVF_VECTOR_STAT("%s-%u.bp_to_bp", ch_stats.bp_to_bp),
	IAVF_VECTOR_STAT("%s-%u.in_intr", ch_stats.in_intr),
	IAVF_VECTOR_STAT("%s-%u.bp_to_intr", ch_stats.bp_to_intr),
	IAVF_VECTOR_STAT("%s-%u.intr_to_intr", ch_stats.intr_to_intr),

	/* unlikely comeback to busy_poll */
	IAVF_VECTOR_STAT("%s-%u.unlikely_cb_to_bp", ch_stats.unlikely_cb_to_bp),
	/* unlikely comeback to busy_poll and once_in_bp is true */
	IAVF_VECTOR_STAT("%s-%u.ucb_once_in_bp_true",
			 ch_stats.ucb_once_in_bp_true),
	/* once_in_bp is false */
	IAVF_VECTOR_STAT("%s-%u.intr_once_in_bp_false",
			 ch_stats.intr_once_bp_false),
	/* busy_poll stop due to need_resched() */
	IAVF_VECTOR_STAT("%s-%u.bp_stop_need_resched",
			 ch_stats.bp_stop_need_resched),
	/* busy_poll stop due to possible due to timeout */
	IAVF_VECTOR_STAT("%s-%u.bp_stop_timeout", ch_stats.bp_stop_timeout),
	/* Transition: BP->INT: previously cleaned data packets */
	IAVF_VECTOR_STAT("%s-%u.cleaned_any_data_pkt",
			 ch_stats.cleaned_any_data_pkt),
	/* need_resched(), but didn't clean any data packets */
	IAVF_VECTOR_STAT("%s-%u.need_resched_no_data_pkt",
			 ch_stats.need_resched_no_data_pkt),
	/* possible timeout(), but didn't clean any data packets */
	IAVF_VECTOR_STAT("%s-%u.timeout_no_data_pkt",
			 ch_stats.timeout_no_data_pkt),
	/* number of SW triggered interrupt from napi_poll due to
	 * possible timeout detected
	 */
	IAVF_VECTOR_STAT("%s-%u.sw_intr_timeout", ch_stats.sw_intr_timeout),
	/* number of SW triggered interrupt from service_task */
	IAVF_VECTOR_STAT("%s-%u.sw_intr_service_task",
			 ch_stats.sw_intr_serv_task),
	/* number of times, SW triggered interrupt is not triggered from
	 * napi_poll even when unlikely_cb_to_bp is set, once_in_bp is set
	 * but ethtool private featute flag is off (for interrupt optimization
	 * strategy
	 */
	IAVF_VECTOR_STAT("%s-%u.no_sw_intr_opt_off",
			 ch_stats.no_sw_intr_opt_off),
	/* number of times WB_ON_ITR is set */
	IAVF_VECTOR_STAT("%s-%u.wb_on_itr_set", ch_stats.wb_on_itr_set),

	/* enable SW triggered interrupt due to not_clean_complete */
	IAVF_VECTOR_STAT("%s-%u.sw_intr_not_cc",
			 ch_stats.intr_en_not_clean_complete),
};

/**
 * iavf_add_one_ethtool_stat - copy the stat into the supplied buffer
 * @data: location to store the stat value
 * @pointer: basis for where to copy from
 * @stat: the stat definition
 *
 * Copies the stat data defined by the pointer and stat structure pair into
 * the memory supplied as data. Used to implement iavf_add_ethtool_stats and
 * iavf_add_queue_stats. If the pointer is null, data will be zero'd.
 */
static void
iavf_add_one_ethtool_stat(u64 *data, void *pointer,
			  const struct iavf_stats *stat)
{
	char *p;

	if (!pointer) {
		/* ensure that the ethtool data buffer is zero'd for any stats
		 * which don't have a valid pointer.
		 */
		*data = 0;
		return;
	}

	p = (char *)pointer + stat->stat_offset;
	switch (stat->sizeof_stat) {
	case sizeof(u64):
		*data = *((u64 *)p);
		break;
	case sizeof(u32):
		*data = *((u32 *)p);
		break;
	case sizeof(u16):
		*data = *((u16 *)p);
		break;
	case sizeof(u8):
		*data = *((u8 *)p);
		break;
	default:
		WARN_ONCE(1, "unexpected stat size for %s",
			  stat->stat_string);
		*data = 0;
	}
}

/**
 * __iavf_add_ethtool_stats - copy stats into the ethtool supplied buffer
 * @data: ethtool stats buffer
 * @pointer: location to copy stats from
 * @stats: array of stats to copy
 * @size: the size of the stats definition
 *
 * Copy the stats defined by the stats array using the pointer as a base into
 * the data buffer supplied by ethtool. Updates the data pointer to point to
 * the next empty location for successive calls to __iavf_add_ethtool_stats.
 * If pointer is null, set the data values to zero and update the pointer to
 * skip these stats.
 **/
static void
__iavf_add_ethtool_stats(u64 **data, void *pointer,
			 const struct iavf_stats stats[],
			 const unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		iavf_add_one_ethtool_stat((*data)++, pointer, &stats[i]);
}

/**
 * iavf_add_ethtool_stats - copy stats into ethtool supplied buffer
 * @data: ethtool stats buffer
 * @pointer: location where stats are stored
 * @stats: static const array of stat definitions
 *
 * Macro to ease the use of __iavf_add_ethtool_stats by taking a static
 * constant stats array and passing the ARRAY_SIZE(). This avoids typos by
 * ensuring that we pass the size associated with the given stats array.
 *
 * The parameter @stats is evaluated twice, so parameters with side effects
 * should be avoided.
 **/
#define iavf_add_ethtool_stats(data, pointer, stats) \
	__iavf_add_ethtool_stats(data, pointer, stats, ARRAY_SIZE(stats))

enum iavf_chnl_stat_type {
	IAVF_CHNL_STAT_INVALID,
	IAVF_CHNL_STAT_POLL,
	IAVF_CHNL_STAT_TX,
	IAVF_CHNL_STAT_RX,
	IAVF_CHNL_STAT_VECTOR,
	IAVF_CHNL_STAT_LAST, /* This must be last */_
};

/**
 * iavf_add_queue_stats_chnl - copy channel specific queue stats
 * @data: ethtool stats buffer
 * @ring: the ring to copy
 * @stat_type: stat_type could be TX/TX/VECTOR
 *
 * Queue statistics must be copied while protected by
 * u64_stats_fetch_begin, so we can't directly use iavf_add_ethtool_stats.
 * Assumes that queue stats are defined in iavf_gstrings_queue_stats. If the
 * ring pointer is null, zero out the queue stat values and update the data
 * pointer. Otherwise safely copy the stats from the ring into the supplied
 * buffer and update the data pointer when finished.
 *
 * This function expects to be called while under rcu_read_lock().
 **/
static void
iavf_add_queue_stats_chnl(u64 **data, struct iavf_ring *ring,
			  enum iavf_chnl_stat_type stat_type)
{
	struct iavf_stats *stats = NULL;
#ifdef HAVE_NDO_GET_STATS64
	unsigned int start;
#endif
	unsigned int size;
	unsigned int i;

	switch (stat_type) {
	case IAVF_CHNL_STAT_POLL:
		size = ARRAY_SIZE(iavf_gstrings_queue_stats_poll);
		stats = iavf_gstrings_queue_stats_poll;
		break;
	case IAVF_CHNL_STAT_TX:
		size = ARRAY_SIZE(iavf_gstrings_queue_stats_tx);
		stats = iavf_gstrings_queue_stats_tx;
		break;
	case IAVF_CHNL_STAT_RX:
		size = ARRAY_SIZE(iavf_gstrings_queue_stats_rx);
		stats = iavf_gstrings_queue_stats_rx;
		break;
	case IAVF_CHNL_STAT_VECTOR:
		size = ARRAY_SIZE(iavf_gstrings_queue_stats_vector);
		stats = iavf_gstrings_queue_stats_vector;
		break;
	default:
		break; /* unsupported stat type */
	}

	if (!stats)
		return;

	/* To avoid invalid statistics values, ensure that we keep retrying
	 * the copy until we get a consistent value according to
	 * u64_stats_fetch_retry. But first, make sure our ring is
	 * non-null before attempting to access its syncp.
	 */
#ifdef HAVE_NDO_GET_STATS64
	do {
		start = !ring ? 0 : u64_stats_fetch_begin(&ring->syncp);
#endif
		for (i = 0; i < size; i++) {
			void *ptr = ring;

			if (stat_type == IAVF_CHNL_STAT_VECTOR)
				ptr = ring ? ring->q_vector : NULL;
			iavf_add_one_ethtool_stat(&(*data)[i], ptr,
						  &stats[i]);
		}
#ifdef HAVE_NDO_GET_STATS64
	} while (ring && u64_stats_fetch_retry(&ring->syncp, start));
#endif

	/* Once we successfully copy the stats in, update the data pointer */
	*data += size;
}

/**
 * iavf_add_queue_stats - copy queue statistics into supplied buffer
 * @data: ethtool stats buffer
 * @ring: the ring to copy
 *
 * Queue statistics must be copied while protected by
 * u64_stats_fetch_begin, so we can't directly use iavf_add_ethtool_stats.
 * Assumes that queue stats are defined in iavf_gstrings_queue_stats. If the
 * ring pointer is null, zero out the queue stat values and update the data
 * pointer. Otherwise safely copy the stats from the ring into the supplied
 * buffer and update the data pointer when finished.
 *
 * This function expects to be called while under rcu_read_lock().
 **/
static void
iavf_add_queue_stats(u64 **data, struct iavf_ring *ring)
{
	const unsigned int size = ARRAY_SIZE(iavf_gstrings_queue_stats);
	const struct iavf_stats *stats = iavf_gstrings_queue_stats;
#ifdef HAVE_NDO_GET_STATS64
	unsigned int start;
#endif
	unsigned int i;

	/* To avoid invalid statistics values, ensure that we keep retrying
	 * the copy until we get a consistent value according to
	 * u64_stats_fetch_retry. But first, make sure our ring is
	 * non-null before attempting to access its syncp.
	 */
#ifdef HAVE_NDO_GET_STATS64
	do {
		start = !ring ? 0 : u64_stats_fetch_begin(&ring->syncp);
#endif
		for (i = 0; i < size; i++)
			iavf_add_one_ethtool_stat(&(*data)[i], ring, &stats[i]);
#ifdef HAVE_NDO_GET_STATS64
	} while (ring && u64_stats_fetch_retry(&ring->syncp, start));
#endif

	/* Once we successfully copy the stats in, update the data pointer */
	*data += size;
}

/**
 * __iavf_add_stat_strings - copy stat strings into ethtool buffer
 * @p: ethtool supplied buffer
 * @stats: stat definitions array
 * @size: size of the stats array
 *
 * Format and copy the strings described by stats into the buffer pointed at
 * by p.
 **/
static void __iavf_add_stat_strings(u8 **p, const struct iavf_stats stats[],
				    const unsigned int size, ...)
{
	unsigned int i;

	for (i = 0; i < size; i++) {
		va_list args;

		va_start(args, size);
		vsnprintf(*p, ETH_GSTRING_LEN, stats[i].stat_string, args);
		*p += ETH_GSTRING_LEN;
		va_end(args);
	}
}

/**
 * iavf_add_stat_strings - copy stat strings into ethtool buffer
 * @p: ethtool supplied buffer
 * @stats: stat definitions array
 *
 * Format and copy the strings described by the const static stats value into
 * the buffer pointed at by p.
 *
 * The parameter @stats is evaluated twice, so parameters with side effects
 * should be avoided. Additionally, stats must be an array such that
 * ARRAY_SIZE can be called on it.
 **/
#define iavf_add_stat_strings(p, stats, ...) \
	__iavf_add_stat_strings(p, stats, ARRAY_SIZE(stats), ## __VA_ARGS__)

#define VF_STAT(_name, _stat) \
	IAVF_STAT(struct iavf_adapter, _name, _stat)

static const struct iavf_stats iavf_gstrings_stats[] = {
	VF_STAT("rx_bytes", current_stats.rx_bytes),
	VF_STAT("rx_unicast", current_stats.rx_unicast),
	VF_STAT("rx_multicast", current_stats.rx_multicast),
	VF_STAT("rx_broadcast", current_stats.rx_broadcast),
	VF_STAT("rx_discards", current_stats.rx_discards),
	VF_STAT("rx_unknown_protocol", current_stats.rx_unknown_protocol),
	VF_STAT("tx_bytes", current_stats.tx_bytes),
	VF_STAT("tx_unicast", current_stats.tx_unicast),
	VF_STAT("tx_multicast", current_stats.tx_multicast),
	VF_STAT("tx_broadcast", current_stats.tx_broadcast),
	VF_STAT("tx_discards", current_stats.tx_discards),
	VF_STAT("tx_errors", current_stats.tx_errors),
	VF_STAT("tx_hwtstamp_skipped", ptp.tx_hwtstamp_skipped),
	VF_STAT("tx_hwtstamp_timeouts", ptp.tx_hwtstamp_timeouts),
#ifdef IAVF_ADD_PROBES
	VF_STAT("tx_tcp_segments", tcp_segs),
	VF_STAT("tx_udp_segments", udp_segs),
	VF_STAT("tx_tcp_cso", tx_tcp_cso),
	VF_STAT("tx_udp_cso", tx_udp_cso),
	VF_STAT("tx_sctp_cso", tx_sctp_cso),
	VF_STAT("tx_ip4_cso", tx_ip4_cso),
	VF_STAT("tx_vlano", tx_vlano),
	VF_STAT("tx_ad_vlano", tx_ad_vlano),
	VF_STAT("rx_tcp_cso", rx_tcp_cso),
	VF_STAT("rx_udp_cso", rx_udp_cso),
	VF_STAT("rx_sctp_cso", rx_sctp_cso),
	VF_STAT("rx_ip4_cso", rx_ip4_cso),
	VF_STAT("rx_vlano", rx_vlano),
	VF_STAT("rx_ad_vlano", rx_ad_vlano),
	VF_STAT("rx_tcp_cso_error", rx_tcp_cso_err),
	VF_STAT("rx_udp_cso_error", rx_udp_cso_err),
	VF_STAT("rx_sctp_cso_error", rx_sctp_cso_err),
	VF_STAT("rx_ip4_cso_error", rx_ip4_cso_err),
#endif
};

#define IAVF_STATS_LEN	ARRAY_SIZE(iavf_gstrings_stats)

#define IAVF_QUEUE_STATS_LEN	(ARRAY_SIZE(iavf_gstrings_queue_stats) + \
				 ARRAY_SIZE(iavf_gstrings_queue_stats_poll))
#define IAVF_TX_QUEUE_STATS_LEN ARRAY_SIZE(iavf_gstrings_queue_stats_tx)
#define IAVF_RX_QUEUE_STATS_LEN ARRAY_SIZE(iavf_gstrings_queue_stats_rx)
#define IAVF_VECTOR_STATS_LEN ARRAY_SIZE(iavf_gstrings_queue_stats_vector)

#ifdef HAVE_SWIOTLB_SKIP_CPU_SYNC
/* For now we have one and only one private flag and it is only defined
 * when we have support for the SKIP_CPU_SYNC DMA attribute.  Instead
 * of leaving all this code sitting around empty we will strip it unless
 * our one private flag is actually available.
 */
struct iavf_priv_flags {
	char flag_string[ETH_GSTRING_LEN];
	u32 flag;
	bool read_only;
};

#define IAVF_PRIV_FLAG(_name, _flag, _read_only) { \
	.flag_string = _name, \
	.flag = _flag, \
	.read_only = _read_only, \
}

static const struct iavf_priv_flags iavf_gstrings_priv_flags[] = {
	IAVF_PRIV_FLAG("legacy-rx", IAVF_FLAG_LEGACY_RX, 0),
};

#define IAVF_PRIV_FLAGS_STR_LEN ARRAY_SIZE(iavf_gstrings_priv_flags)

static const struct iavf_priv_flags iavf_gstrings_chnl_priv_flags[] = {
	IAVF_PRIV_FLAG("channel-pkt-inspect-optimize",
		       IAVF_FLAG_CHNL_PKT_OPT_ENA, 0),
};

#define IAVF_CHNL_PRIV_FLAGS_STR_LEN ARRAY_SIZE(iavf_gstrings_chnl_priv_flags)

#endif /* HAVE_SWIOTLB_SKIP_CPU_SYNC */

/**
 * iavf_get_link_ksettings - Get Link Speed and Duplex settings
 * @netdev: network interface device structure
 * @cmd: ethtool command
 *
 * Reports speed/duplex settings. Because this is a VF, we don't know what
 * kind of link we really have, so we fake it.
 **/
static int iavf_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *cmd)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);

	cmd->base.autoneg = AUTONEG_DISABLE;
	cmd->base.port = PORT_NONE;
	cmd->base.duplex = DUPLEX_FULL;

#ifdef VIRTCHNL_VF_CAP_ADV_LINK_SPEED
	if (ADV_LINK_SUPPORT(adapter)) {
		if (adapter->link_speed_mbps &&
		    adapter->link_speed_mbps < U32_MAX)
			cmd->base.speed = adapter->link_speed_mbps;
		else
			cmd->base.speed = SPEED_UNKNOWN;

		return 0;
	}

#endif /* VIRTCHNL_VF_CAP_ADV_LINK_SPEED */
	switch (adapter->link_speed) {
	case VIRTCHNL_LINK_SPEED_40GB:
		cmd->base.speed = SPEED_40000;
		break;
	case VIRTCHNL_LINK_SPEED_25GB:
#ifdef SPEED_25000
		cmd->base.speed = SPEED_25000;
#else
		netdev_info(netdev,
			    "Speed is 25G, display not supported by this version of ethtool.\n");
#endif
		break;
	case VIRTCHNL_LINK_SPEED_20GB:
		cmd->base.speed = SPEED_20000;
		break;
	case VIRTCHNL_LINK_SPEED_10GB:
		cmd->base.speed = SPEED_10000;
		break;
	case VIRTCHNL_LINK_SPEED_5GB:
		cmd->base.speed = SPEED_5000;
		break;
	case VIRTCHNL_LINK_SPEED_2_5GB:
		cmd->base.speed = SPEED_2500;
		break;
	case VIRTCHNL_LINK_SPEED_1GB:
		cmd->base.speed = SPEED_1000;
		break;
	case VIRTCHNL_LINK_SPEED_100MB:
		cmd->base.speed = SPEED_100;
		break;
	default:
		cmd->base.speed = SPEED_UNKNOWN;
		break;
	}

	return 0;
}

#ifndef ETHTOOL_GLINKSETTINGS
/**
 * iavf_get_settings - Get Link Speed and Duplex settings
 * @netdev: network interface device structure
 * @ecmd: ethtool command
 *
 * Reports speed/duplex settings based on media type.  Since we've backported
 * the new API constructs to use in the old API, this ends up just being
 * a wrapper to iavf_get_link_ksettings.
 **/
static int iavf_get_settings(struct net_device *netdev,
			       struct ethtool_cmd *ecmd)
{
	struct ethtool_link_ksettings ks;

	iavf_get_link_ksettings(netdev, &ks);
	_kc_ethtool_ksettings_to_cmd(&ks, ecmd);
	ecmd->transceiver = XCVR_EXTERNAL;
	return 0;
}
#endif /* !ETHTOOL_GLINKSETTINGS */

/**
 * iavf_get_sset_count - Get length of string set
 * @netdev: network interface device structure
 * @sset: id of string set
 *
 * Reports size of various string tables.
 **/
static int iavf_get_sset_count(struct net_device *netdev, int sset)
{
	/* Report the maximum number queues, even if not every queue is
	 * currently configured. Since allocation of queues is in pairs,
	 * use netdev->real_num_tx_queues * 2. The real_num_tx_queues is set
	 * at device creation and never changes.
	 */

	if (sset == ETH_SS_STATS)
		return IAVF_STATS_LEN +
			(IAVF_QUEUE_STATS_LEN * 2 *
			 netdev->real_num_tx_queues) +
			((IAVF_TX_QUEUE_STATS_LEN + IAVF_RX_QUEUE_STATS_LEN +
			 IAVF_VECTOR_STATS_LEN) * netdev->real_num_tx_queues);
#ifdef HAVE_SWIOTLB_SKIP_CPU_SYNC
	else if (sset == ETH_SS_PRIV_FLAGS)
		return IAVF_PRIV_FLAGS_STR_LEN + IAVF_PRIV_FLAGS_STR_LEN;
#endif
	else
		return -EINVAL;
}

/**
 * iavf_get_ethtool_stats - report device statistics
 * @netdev: network interface device structure
 * @stats: ethtool statistics structure
 * @data: pointer to data buffer
 *
 * All statistics are added to the data buffer as an array of u64.
 **/
static void iavf_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	unsigned int i;

	/* Explicitly request stats refresh */
	iavf_schedule_aq_request(adapter, IAVF_FLAG_AQ_REQUEST_STATS);

	iavf_add_ethtool_stats(&data, adapter, iavf_gstrings_stats);

	rcu_read_lock();
	/* As num_active_queues describe both tx and rx queues, we can use
	 * it to iterate over rings' stats.
	 */
	for (i = 0; i < netdev->real_num_tx_queues; i++) {
		struct iavf_ring *ring;

		/* Tx rings stats */
		ring = &adapter->tx_rings[i];
		iavf_add_queue_stats(&data, ring);
		iavf_add_queue_stats_chnl(&data, ring, IAVF_CHNL_STAT_POLL);
		iavf_add_queue_stats_chnl(&data, ring, IAVF_CHNL_STAT_TX);

		/* Rx rings stats */
		ring = &adapter->rx_rings[i];
		iavf_add_queue_stats(&data, ring);
		iavf_add_queue_stats_chnl(&data, ring, IAVF_CHNL_STAT_POLL);
		iavf_add_queue_stats_chnl(&data, ring, IAVF_CHNL_STAT_RX);
		iavf_add_queue_stats_chnl(&data, ring, IAVF_CHNL_STAT_VECTOR);
	}
	rcu_read_unlock();
}

#ifdef HAVE_SWIOTLB_SKIP_CPU_SYNC
/**
 * iavf_get_priv_flag_strings - Get private flag strings
 * @netdev: network interface device structure
 * @data: buffer for string data
 *
 * Builds the private flags string table
 **/
static void iavf_get_priv_flag_strings(struct net_device *netdev, u8 *data)
{
	unsigned int i;

	for (i = 0; i < IAVF_PRIV_FLAGS_STR_LEN; i++)
		ethtool_sprintf(&data, "%s",
				iavf_gstrings_priv_flags[i].flag_string);
	for (i = 0; i < IAVF_CHNL_PRIV_FLAGS_STR_LEN; i++)
		ethtool_sprintf(&data, "%s",
				iavf_gstrings_chnl_priv_flags[i].flag_string);
}
#endif

/**
 * iavf_get_stat_strings - Get stat strings
 * @netdev: network interface device structure
 * @data: buffer for string data
 *
 * Builds the statistics string table
 **/
static void iavf_get_stat_strings(struct net_device *netdev, u8 *data)
{
	unsigned int i;

	iavf_add_stat_strings(&data, iavf_gstrings_stats);

	/* Queues are always allocated in pairs, so we just use
	 * real_num_tx_queues for both Tx and Rx queues.
	 */
	for (i = 0; i < netdev->real_num_tx_queues; i++) {
		iavf_add_stat_strings(&data, iavf_gstrings_queue_stats,
				      "tx", i);
		iavf_add_stat_strings(&data, iavf_gstrings_queue_stats_poll,
				      "tx", i);
		/* iavf_add_stat_strings(&data, iavf_gstrings_queue_stats_tx,
				      "tx", i);
		*/
		iavf_add_stat_strings(&data, iavf_gstrings_queue_stats,
				      "rx", i);
		iavf_add_stat_strings(&data, iavf_gstrings_queue_stats_poll,
				      "rx", i);
		iavf_add_stat_strings(&data, iavf_gstrings_queue_stats_rx,
				      "rx", i);
		iavf_add_stat_strings(&data, iavf_gstrings_queue_stats_vector,
				      "rx", i);
	}
}

/**
 * iavf_get_strings - Get string set
 * @netdev: network interface device structure
 * @sset: id of string set
 * @data: buffer for string data
 *
 * Builds string tables for various string sets
 **/
static void iavf_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	switch (sset) {
	case ETH_SS_STATS:
		iavf_get_stat_strings(netdev, data);
		break;
#ifdef HAVE_SWIOTLB_SKIP_CPU_SYNC
	case ETH_SS_PRIV_FLAGS:
		iavf_get_priv_flag_strings(netdev, data);
		break;
#endif
	default:
		break;
	}
}

#ifdef HAVE_SWIOTLB_SKIP_CPU_SYNC
/**
 * iavf_get_priv_flags - report device private flags
 * @netdev: network interface device structure
 *
 * The get string set count and the string set should be matched for each
 * flag returned.  Add new strings for each flag to the iavf_gstrings_priv_flags
 * array.
 *
 * Returns a u32 bitmap of flags.
 **/
static u32 iavf_get_priv_flags(struct net_device *netdev)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u32 i, ret_flags = 0;

	for (i = 0; i < IAVF_PRIV_FLAGS_STR_LEN; i++) {
		const struct iavf_priv_flags *priv_flags;

		priv_flags = &iavf_gstrings_priv_flags[i];

		if (priv_flags->flag & adapter->flags)
			ret_flags |= BIT(i);
	}

	for (i = 0; i < IAVF_CHNL_PRIV_FLAGS_STR_LEN; i++) {
		const struct iavf_priv_flags *priv_flags;

		priv_flags = &iavf_gstrings_chnl_priv_flags[i];

		if (priv_flags->flag & adapter->chnl_perf_flags)
			ret_flags |= BIT(i + IAVF_PRIV_FLAGS_STR_LEN);
	}

	return ret_flags;
}

/**
 * iavf_determine_priv_flag_change - detect any change in private flags
 * @priv_flags: Ptr to private flags array
 * @num: count of private flags
 * @bit_offset: "offset" into unified view of bits
 * @flags: bit flags to be set
 * @orig: Ptr to flags
 * @changed_flags: bits changed (based on orig and new value)
 *
 * Detect any changes in priv flags and return those changed bits
 **/
static int
iavf_determine_priv_flag_change(const struct iavf_priv_flags *priv_flags,
				int num, int bit_offset, u32 flags, u32 *orig,
				u32 *changed_flags)
{
	u32 orig_flags = READ_ONCE(*orig);
	u32 new_flags;
	int i;

	new_flags = orig_flags;

	for (i = 0; i < num; i++) {
		if (flags & BIT(i + bit_offset))
			new_flags |= priv_flags->flag;
		else
			new_flags &= ~(priv_flags->flag);

		if (priv_flags->read_only &&
		    ((orig_flags ^ new_flags) & ~BIT(i)))
			return -EOPNOTSUPP;
		priv_flags++;
	}

	/* Before we finalize any flag changes, any checks which we need to
	 * perform to determine if the new flags will be supported should go
	 * here...
	 */

	/* Compare and exchange the new flags into place. If we failed, that
	 * is if cmpxchg returns anything but the old value, this means
	 * something else must have modified the flags variable since we
	 * copied it. We'll just punt with an error and log something in the
	 * message buffer.
	 */
	if (cmpxchg(orig, orig_flags, new_flags) != orig_flags)
		return -EAGAIN;

	*changed_flags = orig_flags ^ new_flags;
	return 0;
}

/**
 * iavf_set_priv_flags - set private flags
 * @netdev: network interface device structure
 * @flags: bit flags to be set
 **/
static int iavf_set_priv_flags(struct net_device *netdev, u32 flags)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u32 changed_chnl_flags;
	u32 changed_flags;
	int ret;

	ret = iavf_determine_priv_flag_change(&iavf_gstrings_priv_flags[0],
					      IAVF_PRIV_FLAGS_STR_LEN, 0,
					      flags, &adapter->flags,
					      &changed_flags);
	if (ret) {
		if (ret == -EAGAIN)
			dev_warn(&adapter->pdev->dev,
				 "Unable to update adapter->flags as it was modified by another thread...\n");
		return ret;
	}

	ret = iavf_determine_priv_flag_change(&iavf_gstrings_chnl_priv_flags[0],
					      IAVF_CHNL_PRIV_FLAGS_STR_LEN,
					      IAVF_PRIV_FLAGS_STR_LEN,
					      flags, &adapter->chnl_perf_flags,
					      &changed_chnl_flags);
	if (ret) {
		if (ret == -EAGAIN)
			dev_warn(&adapter->pdev->dev,
				 "Unable to update adapter->chnl_perf_flags as it was modified by another thread...\n");
		return ret;
	}

	/* Process any additional changes needed as a result of flag changes.
	 * The changed_flags value reflects the list of bits that were changed
	 * in the code above.
	 */

	/* issue a reset to force legacy-rx change to take effect */
	if (changed_flags & IAVF_FLAG_LEGACY_RX) {
		if (netif_running(netdev)) {
			iavf_schedule_reset(adapter, IAVF_FLAG_RESET_NEEDED);
			ret = iavf_wait_for_reset(adapter);
			if (ret)
				netdev_warn(netdev, "Changing private flags timeout or interrupted waiting for reset");
		}
	}
	/* Process any additional changes needed as a result of change
	 * in channel specific flag(s)
	 */
	iavf_setup_ch_info(adapter, changed_chnl_flags);

	return ret;
}
#endif /* HAVE_SWIOTLB_SKIP_CPU_SYNC */
#ifndef HAVE_NDO_SET_FEATURES
/**
 * iavf_get_rx_csum - Get RX checksum settings
 * @netdev: network interface device structure
 *
 * Returns true or false depending upon RX checksum enabled.
 **/
static u32 iavf_get_rx_csum(struct net_device *netdev)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	return adapter->flags & IAVF_FLAG_RX_CSUM_ENABLED;
}

/**
 * iavf_set_rx_csum - Set RX checksum settings
 * @netdev: network interface device structure
 * @data: RX checksum setting (boolean)
 *
 **/
static int iavf_set_rx_csum(struct net_device *netdev, u32 data)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	if (data)
		adapter->flags |= IAVF_FLAG_RX_CSUM_ENABLED;
	else
		adapter->flags &= ~IAVF_FLAG_RX_CSUM_ENABLED;

	return 0;
}

/**
 * iavf_get_tx_csum - Get TX checksum settings
 * @netdev: network interface device structure
 *
 * Returns true or false depending upon TX checksum enabled.
 **/
static u32 iavf_get_tx_csum(struct net_device *netdev)
{
	return (netdev->features & NETIF_F_IP_CSUM) != 0;
}

/**
 * iavf_set_tx_csum - Set TX checksum settings
 * @netdev: network interface device structure
 * @data: TX checksum setting (boolean)
 *
 **/
static int iavf_set_tx_csum(struct net_device *netdev, u32 data)
{
	if (data)
		netdev->features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
	else
		netdev->features &= ~(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);

	return 0;
}

/**
 * iavf_set_tso - Set TX segmentation offload settings
 * @netdev: network interface device structure
 * @data: TSO setting (boolean)
 *
 **/
static int iavf_set_tso(struct net_device *netdev, u32 data)
{
	if (data) {
		netdev->features |= NETIF_F_TSO;
		netdev->features |= NETIF_F_TSO6;
	} else {
		netif_tx_stop_all_queues(netdev);
		netdev->features &= ~NETIF_F_TSO;
		netdev->features &= ~NETIF_F_TSO6;
#ifndef HAVE_NETDEV_VLAN_FEATURES
		/* disable TSO on all VLANs if they're present */
		if (adapter->vsi.vlgrp) {
			struct iavf_adapter *adapter = netdev_priv(netdev);
			struct vlan_group *vlgrp = adapter->vsi.vlgrp;
			struct net_device *v_netdev;
			int i;

			for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
				v_netdev = vlan_group_get_device(vlgrp, i);
				if (v_netdev) {
					v_netdev->features &= ~NETIF_F_TSO;
					v_netdev->features &= ~NETIF_F_TSO6;
					vlan_group_set_device(vlgrp, i,
							      v_netdev);
				}
			}
		}
#endif
		netif_tx_start_all_queues(netdev);
	}
	return 0;
}

#endif /* HAVE_NDO_SET_FEATURES */
/**
 * iavf_get_msglevel - Get debug message level
 * @netdev: network interface device structure
 *
 * Returns current debug message level.
 **/
static u32 iavf_get_msglevel(struct net_device *netdev)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

/**
 * iavf_set_msglevel - Set debug message level
 * @netdev: network interface device structure
 * @data: message level
 *
 * Set current debug message level. Higher values cause the driver to
 * be noisier.
 **/
static void iavf_set_msglevel(struct net_device *netdev, u32 data)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	if (IAVF_DEBUG_USER & data)
		adapter->hw.debug_mask = data;
	adapter->msg_enable = data;
}

/**
 * iavf_get_drvinfo - Get driver info
 * @netdev: network interface device structure
 * @drvinfo: ethool driver info structure
 *
 * Returns information about the driver and device for display to the user.
 **/
static void iavf_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	strscpy(drvinfo->driver, iavf_driver_name, 32);
	strscpy(drvinfo->version, iavf_driver_version, 32);
	strscpy(drvinfo->fw_version, "N/A", 4);
	strscpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
#ifdef HAVE_SWIOTLB_SKIP_CPU_SYNC
	drvinfo->n_priv_flags = IAVF_PRIV_FLAGS_STR_LEN;
#endif
}

/**
 * iavf_get_ringparam - Get ring parameters
 * @netdev: network interface device structure
 * @ring: ethtool ringparam structure
 * @kernel_ring: ethtool extenal ringparam structure
 * @extack: netlink extended ACK report struct
 *
 * Returns current ring parameters. TX and RX rings are reported separately,
 * but the number of rings is not reported.
 **/
#ifdef HAVE_ETHTOOL_EXTENDED_RINGPARAMS
static void iavf_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
#else
static void iavf_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
#endif /* HAVE_ETHTOOL_EXTENDED_RINGPARAMS */
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = IAVF_MAX_RXD;
	ring->tx_max_pending = IAVF_MAX_TXD;
	ring->rx_pending = adapter->rx_desc_count;
	ring->tx_pending = adapter->tx_desc_count;
}

/**
 * iavf_set_ringparam - Set ring parameters
 * @netdev: network interface device structure
 * @ring: ethtool ringparam structure
 * @kernel_ring: ethtool external ringparam structure
 * @extack: netlink extended ACK report struct
 *
 * Sets ring parameters. TX and RX rings are controlled separately, but the
 * number of rings is not specified, so all rings get the same settings.
 **/
#ifdef HAVE_ETHTOOL_EXTENDED_RINGPARAMS
static int iavf_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kernel_ring,
			      struct netlink_ext_ack *extack)
#else
static int iavf_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring)
#endif /* HAVE_ETHTOOL_EXTENDED_RINGPARAMS */
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u32 new_rx_count, new_tx_count;
	int ret = 0;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	if (ring->tx_pending > IAVF_MAX_TXD ||
	    ring->tx_pending < IAVF_MIN_TXD ||
	    ring->rx_pending > IAVF_MAX_RXD ||
	    ring->rx_pending < IAVF_MIN_RXD) {
		netdev_err(netdev, "Descriptors requested (Tx: %d / Rx: %d) out of range [%d-%d] (increment %d)\n",
			   ring->tx_pending, ring->rx_pending, IAVF_MIN_TXD,
			   IAVF_MAX_RXD, IAVF_REQ_DESCRIPTOR_MULTIPLE);
		return -EINVAL;
	}

	new_tx_count = ALIGN(ring->tx_pending, IAVF_REQ_DESCRIPTOR_MULTIPLE);
	if (new_tx_count != ring->tx_pending)
		netdev_info(netdev, "Requested Tx descriptor count rounded up to %d\n",
			    new_tx_count);

	new_rx_count = ALIGN(ring->rx_pending, IAVF_REQ_DESCRIPTOR_MULTIPLE);
	if (new_rx_count != ring->rx_pending)
		netdev_info(netdev, "Requested Rx descriptor count rounded up to %d\n",
			    new_rx_count);

	/* if nothing to do return success */
	if ((new_tx_count == adapter->tx_desc_count) &&
	    (new_rx_count == adapter->rx_desc_count)) {
		netdev_dbg(netdev, "Nothing to change, descriptor count is same as requested\n");
		return 0;
	}

	if (new_tx_count != adapter->tx_desc_count) {
		netdev_dbg(netdev, "Changing Tx descriptor count from %d to %d\n",
			   adapter->tx_desc_count, new_tx_count);
		adapter->tx_desc_count = new_tx_count;
	}

	if (new_rx_count != adapter->rx_desc_count) {
		netdev_dbg(netdev, "Changing Rx descriptor count from %d to %d\n",
			   adapter->rx_desc_count, new_rx_count);
		adapter->rx_desc_count = new_rx_count;
	}

	if (netif_running(netdev)) {
		iavf_schedule_reset(adapter, IAVF_FLAG_RESET_NEEDED);
		ret = iavf_wait_for_reset(adapter);
		if (ret)
			netdev_warn(netdev, "Changing ring parameters timeout or interrupted waiting for reset");
	}

	return ret;
}

/**
 * __iavf_get_coalesce - get per-queue coalesce settings
 * @netdev: the netdev to check
 * @ec: ethtool coalesce data structure
 * @queue: which queue to pick
 *
 * Gets the per-queue settings for coalescence. Specifically Rx and Tx usecs
 * are per queue. If queue is <0 then we default to queue 0 as the
 * representative value.
 **/
static int __iavf_get_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec, int queue)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	struct iavf_ring *rx_ring, *tx_ring;

	/* Rx and Tx usecs per queue value. If user doesn't specify the
	 * queue, return queue 0's value to represent.
	 */
	if (queue < 0)
		queue = 0;
	else if (queue >= adapter->num_active_queues)
		return -EINVAL;

	rx_ring = &adapter->rx_rings[queue];
	tx_ring = &adapter->tx_rings[queue];

	if (ITR_IS_DYNAMIC(rx_ring->itr_setting))
		ec->use_adaptive_rx_coalesce = 1;

	if (ITR_IS_DYNAMIC(tx_ring->itr_setting))
		ec->use_adaptive_tx_coalesce = 1;

	ec->rx_coalesce_usecs = rx_ring->itr_setting & ~IAVF_ITR_DYNAMIC;
	ec->tx_coalesce_usecs = tx_ring->itr_setting & ~IAVF_ITR_DYNAMIC;

	return 0;
}

/**
 * iavf_get_coalesce - Get interrupt coalescing settings
 * @netdev: network interface device structure
 * @ec: ethtool coalesce structure
 * @kernel_coal: ethtool CQE mode setting structure
 * @extack: extack for reporting error messages
 *
 * Returns current coalescing settings. This is referred to elsewhere in the
 * driver as Interrupt Throttle Rate, as this is how the hardware describes
 * this functionality. Note that if per-queue settings have been modified this
 * only represents the settings of queue 0.
 **/
#ifdef HAVE_ETHTOOL_COALESCE_EXTACK
static int iavf_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
#else
static int iavf_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
#endif /* HAVE_ETHTOOL_COALESCE_EXTACK */
{
	return __iavf_get_coalesce(netdev, ec, -1);
}

#ifdef ETHTOOL_PERQUEUE
/**
 * iavf_get_per_queue_coalesce - get coalesce values for specific queue
 * @netdev: netdev to read
 * @ec: coalesce settings from ethtool
 * @queue: the queue to read
 *
 * Read specific queue's coalesce settings.
 **/
static int iavf_get_per_queue_coalesce(struct net_device *netdev, u32 queue,
				       struct ethtool_coalesce *ec)
{
	return __iavf_get_coalesce(netdev, ec, queue);
}
#endif /* ETHTOOL_PERQUEUE */

/**
 * iavf_set_itr_per_queue - set ITR values for specific queue
 * @adapter: the VF adapter struct to set values for
 * @ec: coalesce settings from ethtool
 * @queue: the queue to modify
 *
 * Change the ITR settings for a specific queue.
 **/
static int iavf_set_itr_per_queue(struct iavf_adapter *adapter,
				  struct ethtool_coalesce *ec, int queue)
{
	struct iavf_ring *rx_ring = &adapter->rx_rings[queue];
	struct iavf_ring *tx_ring = &adapter->tx_rings[queue];
	struct iavf_q_vector *q_vector;
	u16 itr_setting;

	itr_setting = rx_ring->itr_setting & ~IAVF_ITR_DYNAMIC;

	if (ec->rx_coalesce_usecs != itr_setting &&
	    ec->use_adaptive_rx_coalesce) {
		netif_info(adapter, drv, adapter->netdev,
			   "Rx interrupt throttling cannot be changed if adaptive-rx is enabled\n");
		return -EINVAL;
	}

	itr_setting = tx_ring->itr_setting & ~IAVF_ITR_DYNAMIC;

	if (ec->tx_coalesce_usecs != itr_setting &&
	    ec->use_adaptive_tx_coalesce) {
		netif_info(adapter, drv, adapter->netdev,
			   "Tx interrupt throttling cannot be changed if adaptive-tx is enabled\n");
		return -EINVAL;
	}

	rx_ring->itr_setting = ITR_REG_ALIGN(ec->rx_coalesce_usecs);
	tx_ring->itr_setting = ITR_REG_ALIGN(ec->tx_coalesce_usecs);

	rx_ring->itr_setting |= IAVF_ITR_DYNAMIC;
	if (!ec->use_adaptive_rx_coalesce)
		rx_ring->itr_setting ^= IAVF_ITR_DYNAMIC;

	tx_ring->itr_setting |= IAVF_ITR_DYNAMIC;
	if (!ec->use_adaptive_tx_coalesce)
		tx_ring->itr_setting ^= IAVF_ITR_DYNAMIC;

	q_vector = rx_ring->q_vector;
	q_vector->rx.target_itr = ITR_TO_REG(rx_ring->itr_setting);

	q_vector = tx_ring->q_vector;
	q_vector->tx.target_itr = ITR_TO_REG(tx_ring->itr_setting);

	/* The interrupt handler itself will take care of programming
	 * the Tx and Rx ITR values based on the values we have entered
	 * into the q_vector, no need to write the values now.
	 */
	return 0;
}

/**
 * __iavf_set_coalesce - set coalesce settings for particular queue
 * @netdev: the netdev to change
 * @ec: ethtool coalesce settings
 * @queue: the queue to change
 *
 * Sets the coalesce settings for a particular queue.
 **/
static int __iavf_set_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec, int queue)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	int i;

	if (ec->rx_coalesce_usecs > IAVF_MAX_ITR) {
		netif_info(adapter, drv, netdev, "Invalid value, rx-usecs range is 0-8160\n");
		return -EINVAL;
	} else if (ec->tx_coalesce_usecs > IAVF_MAX_ITR) {
		netif_info(adapter, drv, netdev, "Invalid value, tx-usecs range is 0-8160\n");
		return -EINVAL;
	}

	/* Rx and Tx usecs has per queue value. If user doesn't specify the
	 * queue, apply to all queues.
	 */
	if (queue < 0) {
		for (i = 0; i < adapter->num_active_queues; i++)
			if (iavf_set_itr_per_queue(adapter, ec, i))
				return -EINVAL;
	} else if (queue < adapter->num_active_queues) {
		if (iavf_set_itr_per_queue(adapter, ec, queue))
			return -EINVAL;
	} else {
		netif_info(adapter, drv, netdev, "Invalid queue value, queue range is 0 - %d\n",
			   adapter->num_active_queues - 1);
		return -EINVAL;
	}

	return 0;
}

/**
 * iavf_set_coalesce - Set interrupt coalescing settings
 * @netdev: network interface device structure
 * @ec: ethtool coalesce structure
 * @kernel_coal: ethtool CQE mode setting structure
 * @extack: extack for reporting error messages
 *
 * Change current coalescing settings for every queue.
 **/
#ifdef HAVE_ETHTOOL_COALESCE_EXTACK
static int iavf_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
#else
static int iavf_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
#endif /* HAVE_ETHTOOL_COALESCE_EXTACK */
{
	return __iavf_set_coalesce(netdev, ec, -1);
}

#ifdef ETHTOOL_PERQUEUE
/**
 * iavf_set_per_queue_coalesce - set specific queue's coalesce settings
 * @netdev: the netdev to change
 * @ec: ethtool's coalesce settings
 * @queue: the queue to modify
 *
 * Modifies a specific queue's coalesce settings.
 */
static int iavf_set_per_queue_coalesce(struct net_device *netdev, u32 queue,
				       struct ethtool_coalesce *ec)
{
	return __iavf_set_coalesce(netdev, ec, queue);
}
#endif /* ETHTOOL_PERQUEUE */

#ifdef ETHTOOL_GRXRINGS
/**
 * iavf_fltr_to_ethtool_flow - convert filter type values to ethtool
 * flow type values
 * @flow: filter type to be converted
 *
 * Returns the corresponding ethtool flow type.
 */
static int iavf_fltr_to_ethtool_flow(enum iavf_fdir_flow_type flow)
{
	switch (flow) {
	case IAVF_FDIR_FLOW_IPV4_TCP:
		return TCP_V4_FLOW;
	case IAVF_FDIR_FLOW_IPV4_UDP:
		return UDP_V4_FLOW;
	case IAVF_FDIR_FLOW_IPV4_SCTP:
		return SCTP_V4_FLOW;
	case IAVF_FDIR_FLOW_IPV4_AH:
		return AH_V4_FLOW;
	case IAVF_FDIR_FLOW_IPV4_ESP:
		return ESP_V4_FLOW;
	case IAVF_FDIR_FLOW_IPV4_OTHER:
		return IPV4_USER_FLOW;
#ifdef HAVE_ETHTOOL_FLOW_UNION_IP6_SPEC
	case IAVF_FDIR_FLOW_IPV6_TCP:
		return TCP_V6_FLOW;
	case IAVF_FDIR_FLOW_IPV6_UDP:
		return UDP_V6_FLOW;
	case IAVF_FDIR_FLOW_IPV6_SCTP:
		return SCTP_V6_FLOW;
	case IAVF_FDIR_FLOW_IPV6_AH:
		return AH_V6_FLOW;
	case IAVF_FDIR_FLOW_IPV6_ESP:
		return ESP_V6_FLOW;
	case IAVF_FDIR_FLOW_IPV6_OTHER:
		return IPV6_USER_FLOW;
#endif /* HAVE_ETHTOOL_FLOW_UNION_IP6_SPEC */
	case IAVF_FDIR_FLOW_NON_IP_L2:
		return ETHER_FLOW;
	default:
		/* 0 is undefined ethtool flow */
		return 0;
	}
}

/**
 * iavf_ethtool_flow_to_fltr - convert ethtool flow type to filter enum
 * @eth: Ethtool flow type to be converted
 *
 * Returns flow enum
 */
static enum iavf_fdir_flow_type iavf_ethtool_flow_to_fltr(int eth)
{
	switch (eth) {
	case TCP_V4_FLOW:
		return IAVF_FDIR_FLOW_IPV4_TCP;
	case UDP_V4_FLOW:
		return IAVF_FDIR_FLOW_IPV4_UDP;
	case SCTP_V4_FLOW:
		return IAVF_FDIR_FLOW_IPV4_SCTP;
	case AH_V4_FLOW:
		return IAVF_FDIR_FLOW_IPV4_AH;
	case ESP_V4_FLOW:
		return IAVF_FDIR_FLOW_IPV4_ESP;
	case IPV4_USER_FLOW:
		return IAVF_FDIR_FLOW_IPV4_OTHER;
#ifdef HAVE_ETHTOOL_FLOW_UNION_IP6_SPEC
	case TCP_V6_FLOW:
		return IAVF_FDIR_FLOW_IPV6_TCP;
	case UDP_V6_FLOW:
		return IAVF_FDIR_FLOW_IPV6_UDP;
	case SCTP_V6_FLOW:
		return IAVF_FDIR_FLOW_IPV6_SCTP;
	case AH_V6_FLOW:
		return IAVF_FDIR_FLOW_IPV6_AH;
	case ESP_V6_FLOW:
		return IAVF_FDIR_FLOW_IPV6_ESP;
	case IPV6_USER_FLOW:
		return IAVF_FDIR_FLOW_IPV6_OTHER;
#endif /* HAVE_ETHTOOL_FLOW_UNION_IP6_SPEC */
	case ETHER_FLOW:
		return IAVF_FDIR_FLOW_NON_IP_L2;
	default:
		return IAVF_FDIR_FLOW_NONE;
	}
}

/**
 * iavf_is_mask_valid - check mask field set
 * @mask: full mask to check
 * @field: field for which mask should be valid
 *
 * If the mask is fully set return true. If it is not valid for field return
 * false.
 */
static bool iavf_is_mask_valid(u64 mask, u64 field)
{
	return (mask & field) == field;
}

/**
 * iavf_parse_rx_flow_user_data - deconstruct user-defined data
 * @fsp: pointer to ethtool Rx flow specification
 * @fltr: pointer to Flow Director filter for userdef data storage
 *
 * Returns 0 on success, negative error value on failure
 */
static int
iavf_parse_rx_flow_user_data(struct ethtool_rx_flow_spec *fsp,
			     struct iavf_fdir_fltr *fltr)
{
	struct iavf_flex_word *flex;
	int i, cnt = 0;

	if (!(fsp->flow_type & FLOW_EXT))
		return 0;

	for (i = 0; i < IAVF_FLEX_WORD_NUM; i++) {
#define IAVF_USERDEF_FLEX_WORD_M	GENMASK(15, 0)
#define IAVF_USERDEF_FLEX_OFFS_S	16
#define IAVF_USERDEF_FLEX_OFFS_M	GENMASK(31, IAVF_USERDEF_FLEX_OFFS_S)
#define IAVF_USERDEF_FLEX_FLTR_M	GENMASK(31, 0)
		u32 value = be32_to_cpu(fsp->h_ext.data[i]);
		u32 mask = be32_to_cpu(fsp->m_ext.data[i]);

		if (!value || !mask)
			continue;

		if (!iavf_is_mask_valid(mask, IAVF_USERDEF_FLEX_FLTR_M))
			return -EINVAL;

		/* 504 is the maximum value for offsets, and offset is measured
		 * from the start of the MAC address.
		 */
#define IAVF_USERDEF_FLEX_MAX_OFFS_VAL 504
		flex = &fltr->flex_words[cnt++];
		flex->word = value & IAVF_USERDEF_FLEX_WORD_M;
		flex->offset = (value & IAVF_USERDEF_FLEX_OFFS_M) >>
			     IAVF_USERDEF_FLEX_OFFS_S;
		if (flex->offset > IAVF_USERDEF_FLEX_MAX_OFFS_VAL)
			return -EINVAL;
	}

	fltr->flex_cnt = cnt;

	return 0;
}

/**
 * iavf_fill_rx_flow_ext_data - fill the additional data
 * @fsp: pointer to ethtool Rx flow specification
 * @fltr: pointer to Flow Director filter to get additional data
 */
static void
iavf_fill_rx_flow_ext_data(struct ethtool_rx_flow_spec *fsp,
			   struct iavf_fdir_fltr *fltr)
{
	if (!fltr->ext_mask.usr_def[0] && !fltr->ext_mask.usr_def[1])
		return;

	fsp->flow_type |= FLOW_EXT;

	memcpy(fsp->h_ext.data, fltr->ext_data.usr_def, sizeof(fsp->h_ext.data));
	memcpy(fsp->m_ext.data, fltr->ext_mask.usr_def, sizeof(fsp->m_ext.data));
}

/**
 * iavf_get_ethtool_fdir_entry - fill ethtool structure with Flow Director filter data
 * @adapter: the VF adapter structure that contains filter list
 * @cmd: ethtool command data structure to receive the filter data
 *
 * Returns 0 as expected for success by ethtool
 */
static int
iavf_get_ethtool_fdir_entry(struct iavf_adapter *adapter,
			    struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;
	struct iavf_fdir_fltr *rule = NULL;
	int ret = 0;

	if (!FDIR_FLTR_SUPPORT(adapter))
		return -EOPNOTSUPP;

	spin_lock_bh(&adapter->fdir_fltr_lock);

	rule = iavf_find_fdir_fltr_by_loc(adapter, fsp->location);
	if (!rule) {
		ret = -EINVAL;
		goto release_lock;
	}

	fsp->flow_type = iavf_fltr_to_ethtool_flow(rule->flow_type);

	memset(&fsp->m_u, 0, sizeof(fsp->m_u));
	memset(&fsp->m_ext, 0, sizeof(fsp->m_ext));

	switch (fsp->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		fsp->h_u.tcp_ip4_spec.ip4src = rule->ip_data.v4_addrs.src_ip;
		fsp->h_u.tcp_ip4_spec.ip4dst = rule->ip_data.v4_addrs.dst_ip;
		fsp->h_u.tcp_ip4_spec.psrc = rule->ip_data.src_port;
		fsp->h_u.tcp_ip4_spec.pdst = rule->ip_data.dst_port;
		fsp->h_u.tcp_ip4_spec.tos = rule->ip_data.tos;
		fsp->m_u.tcp_ip4_spec.ip4src = rule->ip_mask.v4_addrs.src_ip;
		fsp->m_u.tcp_ip4_spec.ip4dst = rule->ip_mask.v4_addrs.dst_ip;
		fsp->m_u.tcp_ip4_spec.psrc = rule->ip_mask.src_port;
		fsp->m_u.tcp_ip4_spec.pdst = rule->ip_mask.dst_port;
		fsp->m_u.tcp_ip4_spec.tos = rule->ip_mask.tos;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		fsp->h_u.ah_ip4_spec.ip4src = rule->ip_data.v4_addrs.src_ip;
		fsp->h_u.ah_ip4_spec.ip4dst = rule->ip_data.v4_addrs.dst_ip;
		fsp->h_u.ah_ip4_spec.spi = rule->ip_data.spi;
		fsp->h_u.ah_ip4_spec.tos = rule->ip_data.tos;
		fsp->m_u.ah_ip4_spec.ip4src = rule->ip_mask.v4_addrs.src_ip;
		fsp->m_u.ah_ip4_spec.ip4dst = rule->ip_mask.v4_addrs.dst_ip;
		fsp->m_u.ah_ip4_spec.spi = rule->ip_mask.spi;
		fsp->m_u.ah_ip4_spec.tos = rule->ip_mask.tos;
		break;
	case IPV4_USER_FLOW:
		fsp->h_u.usr_ip4_spec.ip4src = rule->ip_data.v4_addrs.src_ip;
		fsp->h_u.usr_ip4_spec.ip4dst = rule->ip_data.v4_addrs.dst_ip;
		fsp->h_u.usr_ip4_spec.l4_4_bytes = rule->ip_data.l4_header;
		fsp->h_u.usr_ip4_spec.tos = rule->ip_data.tos;
		fsp->h_u.usr_ip4_spec.ip_ver = ETH_RX_NFC_IP4;
		fsp->h_u.usr_ip4_spec.proto = rule->ip_data.proto;
		fsp->m_u.usr_ip4_spec.ip4src = rule->ip_mask.v4_addrs.src_ip;
		fsp->m_u.usr_ip4_spec.ip4dst = rule->ip_mask.v4_addrs.dst_ip;
		fsp->m_u.usr_ip4_spec.l4_4_bytes = rule->ip_mask.l4_header;
		fsp->m_u.usr_ip4_spec.tos = rule->ip_mask.tos;
		fsp->m_u.usr_ip4_spec.ip_ver = 0xFF;
		fsp->m_u.usr_ip4_spec.proto = rule->ip_mask.proto;
		break;
#ifdef HAVE_ETHTOOL_FLOW_UNION_IP6_SPEC
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
		memcpy(fsp->h_u.usr_ip6_spec.ip6src, &rule->ip_data.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->h_u.usr_ip6_spec.ip6dst, &rule->ip_data.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->h_u.tcp_ip6_spec.psrc = rule->ip_data.src_port;
		fsp->h_u.tcp_ip6_spec.pdst = rule->ip_data.dst_port;
		fsp->h_u.tcp_ip6_spec.tclass = rule->ip_data.tclass;
		memcpy(fsp->m_u.usr_ip6_spec.ip6src, &rule->ip_mask.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.usr_ip6_spec.ip6dst, &rule->ip_mask.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->m_u.tcp_ip6_spec.psrc = rule->ip_mask.src_port;
		fsp->m_u.tcp_ip6_spec.pdst = rule->ip_mask.dst_port;
		fsp->m_u.tcp_ip6_spec.tclass = rule->ip_mask.tclass;
		break;
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
		memcpy(fsp->h_u.ah_ip6_spec.ip6src, &rule->ip_data.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->h_u.ah_ip6_spec.ip6dst, &rule->ip_data.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->h_u.ah_ip6_spec.spi = rule->ip_data.spi;
		fsp->h_u.ah_ip6_spec.tclass = rule->ip_data.tclass;
		memcpy(fsp->m_u.ah_ip6_spec.ip6src, &rule->ip_mask.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.ah_ip6_spec.ip6dst, &rule->ip_mask.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->m_u.ah_ip6_spec.spi = rule->ip_mask.spi;
		fsp->m_u.ah_ip6_spec.tclass = rule->ip_mask.tclass;
		break;
	case IPV6_USER_FLOW:
		memcpy(fsp->h_u.usr_ip6_spec.ip6src, &rule->ip_data.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->h_u.usr_ip6_spec.ip6dst, &rule->ip_data.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->h_u.usr_ip6_spec.l4_4_bytes = rule->ip_data.l4_header;
		fsp->h_u.usr_ip6_spec.tclass = rule->ip_data.tclass;
		fsp->h_u.usr_ip6_spec.l4_proto = rule->ip_data.proto;
		memcpy(fsp->m_u.usr_ip6_spec.ip6src, &rule->ip_mask.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.usr_ip6_spec.ip6dst, &rule->ip_mask.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->m_u.usr_ip6_spec.l4_4_bytes = rule->ip_mask.l4_header;
		fsp->m_u.usr_ip6_spec.tclass = rule->ip_mask.tclass;
		fsp->m_u.usr_ip6_spec.l4_proto = rule->ip_mask.proto;
		break;
#endif /* HAVE_ETHTOOL_FLOW_UNION_IP6_SPEC */
	case ETHER_FLOW:
		fsp->h_u.ether_spec.h_proto = rule->eth_data.etype;
		fsp->m_u.ether_spec.h_proto = rule->eth_mask.etype;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	iavf_fill_rx_flow_ext_data(fsp, rule);

	if (rule->action == VIRTCHNL_ACTION_DROP)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
	else
		fsp->ring_cookie = rule->q_index;

release_lock:
	spin_unlock_bh(&adapter->fdir_fltr_lock);
	return ret;
}

/**
 * iavf_get_fdir_fltr_ids - fill buffer with filter IDs of active filters
 * @adapter: the VF adapter structure containing the filter list
 * @cmd: ethtool command data structure
 * @rule_locs: ethtool array passed in from OS to receive filter IDs
 *
 * Returns 0 as expected for success by ethtool
 */
static int
iavf_get_fdir_fltr_ids(struct iavf_adapter *adapter, struct ethtool_rxnfc *cmd,
		       u32 *rule_locs)
{
	struct iavf_fdir_fltr *fltr;
	unsigned int cnt = 0;
	int val = 0;

	if (!FDIR_FLTR_SUPPORT(adapter))
		return -EOPNOTSUPP;

	cmd->data = IAVF_MAX_FDIR_FILTERS;

	spin_lock_bh(&adapter->fdir_fltr_lock);

	list_for_each_entry(fltr, &adapter->fdir_list_head, list) {
		if (cnt == cmd->rule_cnt) {
			val = -EMSGSIZE;
			goto release_lock;
		}
		rule_locs[cnt] = fltr->loc;
		cnt++;
	}

release_lock:
	spin_unlock_bh(&adapter->fdir_fltr_lock);
	if (!val)
		cmd->rule_cnt = cnt;

	return val;
}

/**
 * iavf_add_fdir_fltr_info - Set the input set for Flow Director filter
 * @adapter: pointer to the VF adapter structure
 * @fsp: pointer to ethtool Rx flow specification
 * @fltr: filter structure
 */
static int
iavf_add_fdir_fltr_info(struct iavf_adapter *adapter, struct ethtool_rx_flow_spec *fsp,
			struct iavf_fdir_fltr *fltr)
{
	u32 flow_type, q_index = 0;
	enum virtchnl_action act;
	int err;

	if (fsp->ring_cookie == RX_CLS_FLOW_DISC) {
		act = VIRTCHNL_ACTION_DROP;
	} else {
		q_index = fsp->ring_cookie;
		if (q_index >= adapter->num_active_queues)
			return -EINVAL;

		act = VIRTCHNL_ACTION_QUEUE;
	}

	fltr->action = act;
	fltr->loc = fsp->location;
	fltr->q_index = q_index;

	if (fsp->flow_type & FLOW_EXT) {
		memcpy(fltr->ext_data.usr_def, fsp->h_ext.data,
		       sizeof(fltr->ext_data.usr_def));
		memcpy(fltr->ext_mask.usr_def, fsp->m_ext.data,
		       sizeof(fltr->ext_mask.usr_def));
	}

#ifdef HAVE_ETHTOOL_FLOW_RSS
	flow_type = fsp->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT | FLOW_RSS);
#else
	flow_type = fsp->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT);
#endif /* HAVE_ETHTOOL_FLOW_RSS */
	fltr->flow_type = iavf_ethtool_flow_to_fltr(flow_type);

	switch (flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		fltr->ip_data.v4_addrs.src_ip = fsp->h_u.tcp_ip4_spec.ip4src;
		fltr->ip_data.v4_addrs.dst_ip = fsp->h_u.tcp_ip4_spec.ip4dst;
		fltr->ip_data.src_port = fsp->h_u.tcp_ip4_spec.psrc;
		fltr->ip_data.dst_port = fsp->h_u.tcp_ip4_spec.pdst;
		fltr->ip_data.tos = fsp->h_u.tcp_ip4_spec.tos;
		fltr->ip_mask.v4_addrs.src_ip = fsp->m_u.tcp_ip4_spec.ip4src;
		fltr->ip_mask.v4_addrs.dst_ip = fsp->m_u.tcp_ip4_spec.ip4dst;
		fltr->ip_mask.src_port = fsp->m_u.tcp_ip4_spec.psrc;
		fltr->ip_mask.dst_port = fsp->m_u.tcp_ip4_spec.pdst;
		fltr->ip_mask.tos = fsp->m_u.tcp_ip4_spec.tos;
		fltr->ip_ver = 4;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		fltr->ip_data.v4_addrs.src_ip = fsp->h_u.ah_ip4_spec.ip4src;
		fltr->ip_data.v4_addrs.dst_ip = fsp->h_u.ah_ip4_spec.ip4dst;
		fltr->ip_data.spi = fsp->h_u.ah_ip4_spec.spi;
		fltr->ip_data.tos = fsp->h_u.ah_ip4_spec.tos;
		fltr->ip_mask.v4_addrs.src_ip = fsp->m_u.ah_ip4_spec.ip4src;
		fltr->ip_mask.v4_addrs.dst_ip = fsp->m_u.ah_ip4_spec.ip4dst;
		fltr->ip_mask.spi = fsp->m_u.ah_ip4_spec.spi;
		fltr->ip_mask.tos = fsp->m_u.ah_ip4_spec.tos;
		fltr->ip_ver = 4;
		break;
	case IPV4_USER_FLOW:
		fltr->ip_data.v4_addrs.src_ip = fsp->h_u.usr_ip4_spec.ip4src;
		fltr->ip_data.v4_addrs.dst_ip = fsp->h_u.usr_ip4_spec.ip4dst;
		fltr->ip_data.l4_header = fsp->h_u.usr_ip4_spec.l4_4_bytes;
		fltr->ip_data.tos = fsp->h_u.usr_ip4_spec.tos;
		fltr->ip_data.proto = fsp->h_u.usr_ip4_spec.proto;
		fltr->ip_mask.v4_addrs.src_ip = fsp->m_u.usr_ip4_spec.ip4src;
		fltr->ip_mask.v4_addrs.dst_ip = fsp->m_u.usr_ip4_spec.ip4dst;
		fltr->ip_mask.l4_header = fsp->m_u.usr_ip4_spec.l4_4_bytes;
		fltr->ip_mask.tos = fsp->m_u.usr_ip4_spec.tos;
		fltr->ip_mask.proto = fsp->m_u.usr_ip4_spec.proto;
		fltr->ip_ver = 4;
		break;
#ifdef HAVE_ETHTOOL_FLOW_UNION_IP6_SPEC
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
		memcpy(&fltr->ip_data.v6_addrs.src_ip, fsp->h_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_data.v6_addrs.dst_ip, fsp->h_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_data.src_port = fsp->h_u.tcp_ip6_spec.psrc;
		fltr->ip_data.dst_port = fsp->h_u.tcp_ip6_spec.pdst;
		fltr->ip_data.tclass = fsp->h_u.tcp_ip6_spec.tclass;
		memcpy(&fltr->ip_mask.v6_addrs.src_ip, fsp->m_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_mask.v6_addrs.dst_ip, fsp->m_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_mask.src_port = fsp->m_u.tcp_ip6_spec.psrc;
		fltr->ip_mask.dst_port = fsp->m_u.tcp_ip6_spec.pdst;
		fltr->ip_mask.tclass = fsp->m_u.tcp_ip6_spec.tclass;
		fltr->ip_ver = 6;
		break;
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
		memcpy(&fltr->ip_data.v6_addrs.src_ip, fsp->h_u.ah_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_data.v6_addrs.dst_ip, fsp->h_u.ah_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_data.spi = fsp->h_u.ah_ip6_spec.spi;
		fltr->ip_data.tclass = fsp->h_u.ah_ip6_spec.tclass;
		memcpy(&fltr->ip_mask.v6_addrs.src_ip, fsp->m_u.ah_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_mask.v6_addrs.dst_ip, fsp->m_u.ah_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_mask.spi = fsp->m_u.ah_ip6_spec.spi;
		fltr->ip_mask.tclass = fsp->m_u.ah_ip6_spec.tclass;
		fltr->ip_ver = 6;
		break;
	case IPV6_USER_FLOW:
		memcpy(&fltr->ip_data.v6_addrs.src_ip, fsp->h_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_data.v6_addrs.dst_ip, fsp->h_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_data.l4_header = fsp->h_u.usr_ip6_spec.l4_4_bytes;
		fltr->ip_data.tclass = fsp->h_u.usr_ip6_spec.tclass;
		fltr->ip_data.proto = fsp->h_u.usr_ip6_spec.l4_proto;
		memcpy(&fltr->ip_mask.v6_addrs.src_ip, fsp->m_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_mask.v6_addrs.dst_ip, fsp->m_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_mask.l4_header = fsp->m_u.usr_ip6_spec.l4_4_bytes;
		fltr->ip_mask.tclass = fsp->m_u.usr_ip6_spec.tclass;
		fltr->ip_mask.proto = fsp->m_u.usr_ip6_spec.l4_proto;
		fltr->ip_ver = 6;
		break;
#endif /* HAVE_ETHTOOL_FLOW_UNION_IP6_SPEC */
	case ETHER_FLOW:
		fltr->eth_data.etype = fsp->h_u.ether_spec.h_proto;
		fltr->eth_mask.etype = fsp->m_u.ether_spec.h_proto;
		break;
	default:
		/* not doing un-parsed flow types */
		return -EINVAL;
	}

	err = iavf_validate_fdir_fltr_masks(adapter, fltr);
	if (err)
		return err;

	if (iavf_fdir_is_dup_fltr(adapter, fltr))
		return -EEXIST;

	err = iavf_parse_rx_flow_user_data(fsp, fltr);
	if (err)
		return err;

	return iavf_fill_fdir_add_msg(adapter, fltr);
}

/**
 * iavf_add_fdir_ethtool - add Flow Director filter
 * @adapter: pointer to the VF adapter structure
 * @cmd: command to add Flow Director filter
 *
 * Returns 0 on success and negative values for failure
 */
static int iavf_add_fdir_ethtool(struct iavf_adapter *adapter, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = &cmd->fs;
	struct iavf_fdir_fltr *fltr;
	int count = 50;
	int err;

	if (!FDIR_FLTR_SUPPORT(adapter))
		return -EOPNOTSUPP;

	if (fsp->flow_type & FLOW_MAC_EXT)
		return -EINVAL;

	spin_lock_bh(&adapter->fdir_fltr_lock);
	if (adapter->fdir_active_fltr >= IAVF_MAX_FDIR_FILTERS) {
		spin_unlock_bh(&adapter->fdir_fltr_lock);
		dev_err(&adapter->pdev->dev,
			"Unable to add Flow Director filter because VF reached the limit of max allowed filters (%u)\n",
			IAVF_MAX_FDIR_FILTERS);
		return -ENOSPC;
	}

	if (iavf_find_fdir_fltr_by_loc(adapter, fsp->location)) {
		dev_err(&adapter->pdev->dev, "Failed to add Flow Director filter, it already exists\n");
		spin_unlock_bh(&adapter->fdir_fltr_lock);
		return -EEXIST;
	}
	spin_unlock_bh(&adapter->fdir_fltr_lock);

	fltr = kzalloc(sizeof(*fltr), GFP_KERNEL);
	if (!fltr)
		return -ENOMEM;

	while (!mutex_trylock(&adapter->crit_lock)) {
		if (--count == 0) {
			kfree(fltr);
			return -EINVAL;
		}
		udelay(1);
	}

	err = iavf_add_fdir_fltr_info(adapter, fsp, fltr);
	if (err)
		goto ret;

	spin_lock_bh(&adapter->fdir_fltr_lock);
	iavf_fdir_list_add_fltr(adapter, fltr);
	adapter->fdir_active_fltr++;
	fltr->state = IAVF_FDIR_FLTR_ADD_REQUEST;
	adapter->aq_required |= IAVF_FLAG_AQ_ADD_FDIR_FILTER;
	spin_unlock_bh(&adapter->fdir_fltr_lock);

	mod_delayed_work(adapter->wq, &adapter->watchdog_task, 0);

ret:
	if (err && fltr)
		kfree(fltr);

	mutex_unlock(&adapter->crit_lock);
	return err;
}

/**
 * iavf_del_fdir_ethtool - delete Flow Director filter
 * @adapter: pointer to the VF adapter structure
 * @cmd: command to delete Flow Director filter
 *
 * Returns 0 on success and negative values for failure
 */
static int iavf_del_fdir_ethtool(struct iavf_adapter *adapter, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;
	struct iavf_fdir_fltr *fltr = NULL;
	int err = 0;

	if (!FDIR_FLTR_SUPPORT(adapter))
		return -EOPNOTSUPP;

	spin_lock_bh(&adapter->fdir_fltr_lock);
	fltr = iavf_find_fdir_fltr_by_loc(adapter, fsp->location);
	if (fltr) {
		if (fltr->state == IAVF_FDIR_FLTR_ACTIVE) {
			fltr->state = IAVF_FDIR_FLTR_DEL_REQUEST;
			adapter->aq_required |= IAVF_FLAG_AQ_DEL_FDIR_FILTER;
		} else {
			err = -EBUSY;
		}
	} else if (adapter->fdir_active_fltr) {
		err = -EINVAL;
	}
	spin_unlock_bh(&adapter->fdir_fltr_lock);

	if (fltr && fltr->state == IAVF_FDIR_FLTR_DEL_REQUEST)
		mod_delayed_work(adapter->wq, &adapter->watchdog_task, 0);

	return err;
}

/**
 * iavf_adv_rss_parse_hdrs - parses headers from RSS hash input
 * @cmd: ethtool rxnfc command
 *
 * This function parses the rxnfc command and returns intended
 * header types for RSS configuration
 */
static u32 iavf_adv_rss_parse_hdrs(struct ethtool_rxnfc *cmd)
{
	u32 hdrs = IAVF_ADV_RSS_FLOW_SEG_HDR_NONE;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_TCP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV4;
		break;
	case UDP_V4_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_UDP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV4;
		break;
	case SCTP_V4_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_SCTP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV4;
		break;
	case TCP_V6_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_TCP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV6;
		break;
	case UDP_V6_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_UDP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV6;
		break;
	case SCTP_V6_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_SCTP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV6;
		break;
	default:
		break;
	}

	return hdrs;
}

/**
 * iavf_adv_rss_parse_hash_flds - parses hash fields from RSS hash input
 * @cmd: ethtool rxnfc command
 *
 * This function parses the rxnfc command and returns intended hash fields for
 * RSS configuration
 */
static u64 iavf_adv_rss_parse_hash_flds(struct ethtool_rxnfc *cmd)
{
	u64 hfld = IAVF_ADV_RSS_HASH_INVALID;

	if (cmd->data & RXH_IP_SRC || cmd->data & RXH_IP_DST) {
		switch (cmd->flow_type) {
		case TCP_V4_FLOW:
		case UDP_V4_FLOW:
		case SCTP_V4_FLOW:
			if (cmd->data & RXH_IP_SRC)
				hfld |= IAVF_ADV_RSS_HASH_FLD_IPV4_SA;
			if (cmd->data & RXH_IP_DST)
				hfld |= IAVF_ADV_RSS_HASH_FLD_IPV4_DA;
			break;
		case TCP_V6_FLOW:
		case UDP_V6_FLOW:
		case SCTP_V6_FLOW:
			if (cmd->data & RXH_IP_SRC)
				hfld |= IAVF_ADV_RSS_HASH_FLD_IPV6_SA;
			if (cmd->data & RXH_IP_DST)
				hfld |= IAVF_ADV_RSS_HASH_FLD_IPV6_DA;
			break;
		default:
			break;
		}
	}

	if (cmd->data & RXH_L4_B_0_1 || cmd->data & RXH_L4_B_2_3) {
		switch (cmd->flow_type) {
		case TCP_V4_FLOW:
		case TCP_V6_FLOW:
			if (cmd->data & RXH_L4_B_0_1)
				hfld |= IAVF_ADV_RSS_HASH_FLD_TCP_SRC_PORT;
			if (cmd->data & RXH_L4_B_2_3)
				hfld |= IAVF_ADV_RSS_HASH_FLD_TCP_DST_PORT;
			break;
		case UDP_V4_FLOW:
		case UDP_V6_FLOW:
			if (cmd->data & RXH_L4_B_0_1)
				hfld |= IAVF_ADV_RSS_HASH_FLD_UDP_SRC_PORT;
			if (cmd->data & RXH_L4_B_2_3)
				hfld |= IAVF_ADV_RSS_HASH_FLD_UDP_DST_PORT;
			break;
		case SCTP_V4_FLOW:
		case SCTP_V6_FLOW:
			if (cmd->data & RXH_L4_B_0_1)
				hfld |= IAVF_ADV_RSS_HASH_FLD_SCTP_SRC_PORT;
			if (cmd->data & RXH_L4_B_2_3)
				hfld |= IAVF_ADV_RSS_HASH_FLD_SCTP_DST_PORT;
			break;
		default:
			break;
		}
	}

	return hfld;
}

/**
 * iavf_set_adv_rss_hash_opt - Enable/Disable flow types for RSS hash
 * @adapter: pointer to the VF adapter structure
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the flow input set is supported.
 */
static int
iavf_set_adv_rss_hash_opt(struct iavf_adapter *adapter,
			  struct ethtool_rxnfc *cmd)
{
	struct iavf_adv_rss *rss_old, *rss_new;
	bool rss_new_add = false;
	int count = 50, err = 0;
	u64 hash_flds;
	u32 hdrs;

	if (!ADV_RSS_SUPPORT(adapter))
		return -EOPNOTSUPP;

	hdrs = iavf_adv_rss_parse_hdrs(cmd);
	if (hdrs == IAVF_ADV_RSS_FLOW_SEG_HDR_NONE)
		return -EINVAL;

	hash_flds = iavf_adv_rss_parse_hash_flds(cmd);
	if (hash_flds == IAVF_ADV_RSS_HASH_INVALID)
		return -EINVAL;

	rss_new = kzalloc(sizeof(*rss_new), GFP_KERNEL);
	if (!rss_new)
		return -ENOMEM;

	if (iavf_fill_adv_rss_cfg_msg(&rss_new->cfg_msg, hdrs, hash_flds)) {
		kfree(rss_new);
		return -EINVAL;
	}

	while (!mutex_trylock(&adapter->crit_lock)) {
		if (--count == 0) {
			kfree(rss_new);
			return -EINVAL;
		}

		udelay(1);
	}

	spin_lock_bh(&adapter->adv_rss_lock);
	rss_old = iavf_find_adv_rss_cfg_by_hdrs(adapter, hdrs);
	if (rss_old) {
		if (rss_old->state != IAVF_ADV_RSS_ACTIVE) {
			err = -EBUSY;
		} else if (rss_old->hash_flds != hash_flds) {
			rss_old->state = IAVF_ADV_RSS_ADD_REQUEST;
			rss_old->hash_flds = hash_flds;
			memcpy(&rss_old->cfg_msg, &rss_new->cfg_msg,
			       sizeof(rss_new->cfg_msg));
			adapter->aq_required |= IAVF_FLAG_AQ_ADD_ADV_RSS_CFG;
		} else {
			err = -EEXIST;
		}
	} else {
		rss_new_add = true;
		rss_new->state = IAVF_ADV_RSS_ADD_REQUEST;
		rss_new->packet_hdrs = hdrs;
		rss_new->hash_flds = hash_flds;
		list_add_tail(&rss_new->list, &adapter->adv_rss_list_head);
		adapter->aq_required |= IAVF_FLAG_AQ_ADD_ADV_RSS_CFG;
	}
	spin_unlock_bh(&adapter->adv_rss_lock);

	if (!err)
		mod_delayed_work(adapter->wq, &adapter->watchdog_task, 0);

	mutex_unlock(&adapter->crit_lock);

	if (!rss_new_add)
		kfree(rss_new);

	return err;
}

/**
 * iavf_get_adv_rss_hash_opt - Retrieve hash fields for a given flow-type
 * @adapter: pointer to the VF adapter structure
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the flow input set is supported.
 */
static int
iavf_get_adv_rss_hash_opt(struct iavf_adapter *adapter,
			  struct ethtool_rxnfc *cmd)
{
	struct iavf_adv_rss *rss;
	u64 hash_flds;
	u32 hdrs;

	if (!ADV_RSS_SUPPORT(adapter))
		return -EOPNOTSUPP;

	cmd->data = 0;

	hdrs = iavf_adv_rss_parse_hdrs(cmd);
	if (hdrs == IAVF_ADV_RSS_FLOW_SEG_HDR_NONE)
		return -EINVAL;

	spin_lock_bh(&adapter->adv_rss_lock);
	rss = iavf_find_adv_rss_cfg_by_hdrs(adapter, hdrs);
	if (rss)
		hash_flds = rss->hash_flds;
	else
		hash_flds = IAVF_ADV_RSS_HASH_INVALID;
	spin_unlock_bh(&adapter->adv_rss_lock);

	if (hash_flds == IAVF_ADV_RSS_HASH_INVALID)
		return -EINVAL;

	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_IPV4_SA |
			 IAVF_ADV_RSS_HASH_FLD_IPV6_SA))
		cmd->data |= (u64)RXH_IP_SRC;

	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_IPV4_DA |
			 IAVF_ADV_RSS_HASH_FLD_IPV6_DA))
		cmd->data |= (u64)RXH_IP_DST;

	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_TCP_SRC_PORT |
			 IAVF_ADV_RSS_HASH_FLD_UDP_SRC_PORT |
			 IAVF_ADV_RSS_HASH_FLD_SCTP_SRC_PORT))
		cmd->data |= (u64)RXH_L4_B_0_1;

	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_TCP_DST_PORT |
			 IAVF_ADV_RSS_HASH_FLD_UDP_DST_PORT |
			 IAVF_ADV_RSS_HASH_FLD_SCTP_DST_PORT))
		cmd->data |= (u64)RXH_L4_B_2_3;

	return 0;
}

/**
 * iavf_set_rxnfc - command to set Rx flow rules.
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 *
 * Returns 0 for success and negative values for errors
 */
static int iavf_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		ret = iavf_add_fdir_ethtool(adapter, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = iavf_del_fdir_ethtool(adapter, cmd);
		break;
	case ETHTOOL_SRXFH:
		ret = iavf_set_adv_rss_hash_opt(adapter, cmd);
		break;
	default:
		break;
	}

	return ret;
}

/**
 * iavf_get_rxnfc - command to get RX flow classification rules
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 * @rule_locs: pointer to store rule locations
 *
 * Returns Success if the command is supported.
 **/
static int iavf_get_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd,
#ifdef HAVE_ETHTOOL_GET_RXNFC_VOID_RULE_LOCS
			  void *rule_locs)
#else
			  u32 *rule_locs)
#endif
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = adapter->num_active_queues;
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		if (!FDIR_FLTR_SUPPORT(adapter))
			break;
		spin_lock_bh(&adapter->fdir_fltr_lock);
		cmd->rule_cnt = adapter->fdir_active_fltr;
		spin_unlock_bh(&adapter->fdir_fltr_lock);
		cmd->data = IAVF_MAX_FDIR_FILTERS;
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = iavf_get_ethtool_fdir_entry(adapter, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = iavf_get_fdir_fltr_ids(adapter, cmd, (u32 *)rule_locs);
		break;
	case ETHTOOL_GRXFH:
		ret = iavf_get_adv_rss_hash_opt(adapter, cmd);
		break;
	default:
		break;
	}

	return ret;
}
#endif /* ETHTOOL_GRXRINGS */
#ifdef ETHTOOL_GCHANNELS
/**
 * iavf_get_channels: get the number of channels supported by the device
 * @netdev: network interface device structure
 * @ch: channel information structure
 *
 * For the purposes of our device, we only use combined channels, i.e. a tx/rx
 * queue pair. Report one extra channel to match our "other" MSI-X vector.
 **/
static void iavf_get_channels(struct net_device *netdev,
			      struct ethtool_channels *ch)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	int max_channels;

	/* We cannot use more than either of these */
	max_channels = min_t(int, netdev->num_tx_queues,
			     adapter->vsi_res->num_queue_pairs);
	ch->max_combined = max_channels;
	if (iavf_is_adq_enabled(adapter))
		ch->max_combined = adapter->num_max_queue_pairs;

	ch->max_other = NONQ_VECS;
	ch->other_count = NONQ_VECS;

	ch->combined_count = adapter->num_active_queues;
}

/**
 * iavf_set_channels: set the new channel count
 * @netdev: network interface device structure
 * @ch: channel information structure
 *
 * Negotiate a new number of channels with the PF then do a reset.  During
 * reset we'll realloc queues and fix the RSS table.  Returns 0 on success,
 * negative on failure.
 **/
static int iavf_set_channels(struct net_device *netdev,
			     struct ethtool_channels *ch)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u32 num_req = ch->combined_count;
	struct iavf_fdir_fltr *rule;
	int ret = 0;

#ifdef __TC_MQPRIO_MODE_MAX
	if (iavf_is_adq_enabled(adapter)) {
		dev_info(&adapter->pdev->dev, "Cannot set channels since ADQ is enabled.\n");
		return -EOPNOTSUPP;
	}
#endif /* __TC_MQPRIO_MODE_MAX */

	/* All of these should have already been checked by ethtool before this
	 * even gets to us, but just to be sure.
	 */
	if (num_req == 0 || num_req > netdev->num_tx_queues)
		return -EINVAL;

	if (num_req == adapter->num_active_queues)
		return 0;

	if (ch->rx_count || ch->tx_count || ch->other_count != NONQ_VECS)
		return -EINVAL;

	spin_lock_bh(&adapter->fdir_fltr_lock);
	list_for_each_entry(rule, &adapter->fdir_list_head, list) {
		if (rule->action == VIRTCHNL_ACTION_QUEUE &&
		    rule->q_index >= num_req) {
			ret = -EINVAL;
			break;
		}
	}
	spin_unlock_bh(&adapter->fdir_fltr_lock);

	if (ret) {
		netdev_err(netdev, "Cannot set channels, Flow Director filters are conflicting with new queue count\n");
		return ret;
	}

	adapter->num_req_queues = num_req;
	adapter->flags |= IAVF_FLAG_REINIT_ITR_NEEDED;
	iavf_schedule_reset(adapter, IAVF_FLAG_RESET_NEEDED);

	ret = iavf_wait_for_reset(adapter);
	if (ret)
		netdev_warn(netdev, "Changing channel count timeout or interrupted waiting for reset");

	return ret;
}

#endif /* ETHTOOL_GCHANNELS */

#if defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH)
/**
 * iavf_get_rxfh_key_size - get the RSS hash key size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 **/
static u32 iavf_get_rxfh_key_size(struct net_device *netdev)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	return adapter->rss_key_size;
}

/**
 * iavf_get_rxfh_indir_size - get the rx flow hash indirection table size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 **/
static u32 iavf_get_rxfh_indir_size(struct net_device *netdev)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	return adapter->rss_lut_size;
}

#ifdef HAVE_ETHTOOL_RXFH_PARAM
/**
 * iavf_get_rxfh - get the rx flow hash indirection table
 * @netdev: network interface device structure
 * @rxfh: pointer to param struct (indir, key, hfunc)
 *
 * Reads the indirection table directly from the hardware. Always returns 0.
 **/
static int iavf_get_rxfh(struct net_device *netdev,
			 struct ethtool_rxfh_param *rxfh)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u16 i;

	rxfh->hfunc = ETH_RSS_HASH_TOP;
	if (rxfh->key)
		memcpy(rxfh->key, adapter->rss_key, adapter->rss_key_size);

	if (rxfh->indir)
		/* Each 32 bits pointed by 'indir' is stored with a lut entry */
		for (i = 0; i < adapter->rss_lut_size; i++)
			rxfh->indir[i] = (u32)adapter->rss_lut[i];

	return 0;
}
#else
/**
 * iavf_get_rxfh - get the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 * @hfunc: hash function in use
 *
 * Reads the indirection table directly from the hardware. Always returns 0.
 **/
#ifdef HAVE_RXFH_HASHFUNC
static int iavf_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			 u8 *hfunc)
#else
static int iavf_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key)
#endif
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u16 i;

#ifdef HAVE_RXFH_HASHFUNC
	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;
#endif
	if (key)
		memcpy(key, adapter->rss_key, adapter->rss_key_size);

	if (indir)
		/* Each 32 bits pointed by 'indir' is stored with a lut entry */
		for (i = 0; i < adapter->rss_lut_size; i++)
			indir[i] = (u32)adapter->rss_lut[i];

	return 0;
}
#endif /* HAVE_ETHTOOL_RXFH_PARAM */

#ifdef HAVE_ETHTOOL_RXFH_PARAM
/**
 * iavf_set_rxfh - set the rx flow hash indirection table
 * @netdev: network interface device structure
 * @rxfh: pointer to param struct (indir, key, hfunc)
 * @extack: extended ACK from the Netlink message
 *
 * Returns -EINVAL if the table specifies an invalid queue id, otherwise
 * returns 0 after programming the table.
 **/
static int iavf_set_rxfh(struct net_device *netdev,
			 struct ethtool_rxfh_param *rxfh,
			 struct netlink_ext_ack *extack)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u16 i;

#ifdef __TC_MQPRIO_MODE_MAX
	if (iavf_is_adq_enabled(adapter)) {
		dev_info(&adapter->pdev->dev,
			 "Change in RSS params is not supported when ADQ is configured.\n");
		return -EOPNOTSUPP;
	}
#endif /* __TC_MQPRIO_MODE_MAX */
	/* Only support toeplitz hash function */
	if (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE &&
	    rxfh->hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (!rxfh->key && !rxfh->indir)
		return 0;

	if (rxfh->key)
		memcpy(adapter->rss_key, rxfh->key, adapter->rss_key_size);

	if (rxfh->indir) {
		/* Verify user input. */
		for (i = 0; i < adapter->rss_lut_size; i++) {
			if (rxfh->indir[i] >= adapter->num_active_queues)
				return -EINVAL;
		}
		/* Each 32 bits pointed by 'indir' is stored with a lut entry */
		for (i = 0; i < adapter->rss_lut_size; i++)
			adapter->rss_lut[i] = (u8)(rxfh->indir[i]);
	}

	return iavf_config_rss(adapter);
}
#else
/**
 * iavf_set_rxfh - set the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 * @hfunc: hash function to use
 *
 * Returns -EINVAL if the table specifies an invalid queue id, otherwise
 * returns 0 after programming the table.
 **/
#ifdef HAVE_RXFH_HASHFUNC
static int iavf_set_rxfh(struct net_device *netdev, const u32 *indir,
			 const u8 *key, const u8 hfunc)
#else
#ifdef HAVE_RXFH_NONCONST
static int iavf_set_rxfh(struct net_device *netdev, u32 *indir, u8 *key)
#else
static int iavf_set_rxfh(struct net_device *netdev, const u32 *indir,
			 const u8 *key)
#endif /* HAVE_RXFH_NONCONST */
#endif /* HAVE_RXFH_HASHFUNC */
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u16 i;

#ifdef __TC_MQPRIO_MODE_MAX
	if (iavf_is_adq_enabled(adapter)) {
		dev_info(&adapter->pdev->dev,
			 "Change in RSS params is not supported when ADQ is configured.\n");
		return -EOPNOTSUPP;
	}
#endif /* __TC_MQPRIO_MODE_MAX */

#ifdef HAVE_RXFH_HASHFUNC
	/* Only support toeplitz hash function */
	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;
#endif

	if (indir) {
		/* Verify user input. */
		for (i = 0; i < adapter->rss_lut_size; i++) {
			if (indir[i] >= adapter->num_active_queues)
				return -EINVAL;
		}
	}

	if (!key && !indir)
		return 0;

	if (key)
		memcpy(adapter->rss_key, key, adapter->rss_key_size);

	if (indir) {
		/* Each 32 bits pointed by 'indir' is stored with a lut entry */
		for (i = 0; i < adapter->rss_lut_size; i++)
			adapter->rss_lut[i] = (u8)(indir[i]);
	}

	return iavf_config_rss(adapter);
}
#endif /* ETHTOOL_GRSSH && ETHTOOL_SRSSH */
#endif /* HAVE_ETHTOOL_RXFH_PARAM */

#ifdef HAVE_ETHTOOL_GET_TS_INFO
#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
/**
 * iavf_get_ts_info - Report available timestamping capabilities
 * @netdev: the netdevice to report for
 * @info: structure to fill in
 *
 * Based on device features enabled, report the Tx and Rx timestamp
 * capabilities, as well as the PTP hardware clock index to user space.
 */
static int iavf_get_ts_info(struct net_device *netdev, struct ethtool_ts_info *info)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE;

	if (iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_TX_TSTAMP)) {
		info->so_timestamping |= SOF_TIMESTAMPING_TX_HARDWARE |
					 SOF_TIMESTAMPING_RAW_HARDWARE;
		info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);
	}

	/* Rx timestamps are only supported on the flexible descriptors. Do
	 * not report support unless we both have the capability and
	 * configured with the appropriate descriptor format
	 */
	if (iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_RX_TSTAMP) &&
	    adapter->rxdid == VIRTCHNL_RXDID_2_FLEX_SQ_NIC) {
		info->so_timestamping |= SOF_TIMESTAMPING_RX_HARDWARE |
					 SOF_TIMESTAMPING_RAW_HARDWARE;
		info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) | BIT(HWTSTAMP_FILTER_ALL);
	}

	if (adapter->ptp.initialized)
		info->phc_index = ptp_clock_index(adapter->ptp.clock);
	else
		info->phc_index = -1;

	return 0;
}
#endif /* CONFIG_PTP_1588_CLOCK */
#endif /* HAVE_ETHTOOL_GET_TS_INFO */

static const struct ethtool_ops iavf_ethtool_ops = {
#ifdef ETHTOOL_COALESCE_USECS
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_USE_ADAPTIVE,
#endif /* ETHTOOL_COALESCE_USECS */
	.get_drvinfo		= iavf_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_ringparam		= iavf_get_ringparam,
	.set_ringparam		= iavf_set_ringparam,
#ifndef HAVE_NDO_SET_FEATURES
	.get_rx_csum		= iavf_get_rx_csum,
	.set_rx_csum		= iavf_set_rx_csum,
	.get_tx_csum		= iavf_get_tx_csum,
	.set_tx_csum		= iavf_set_tx_csum,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= iavf_set_tso,
#endif /* HAVE_NDO_SET_FEATURES */
	.get_strings		= iavf_get_strings,
	.get_ethtool_stats	= iavf_get_ethtool_stats,
	.get_sset_count		= iavf_get_sset_count,
#ifdef HAVE_SWIOTLB_SKIP_CPU_SYNC
	.get_priv_flags		= iavf_get_priv_flags,
	.set_priv_flags		= iavf_set_priv_flags,
#endif
	.get_msglevel		= iavf_get_msglevel,
	.set_msglevel		= iavf_set_msglevel,
	.get_coalesce		= iavf_get_coalesce,
	.set_coalesce		= iavf_set_coalesce,
#ifdef ETHTOOL_PERQUEUE
	.get_per_queue_coalesce = iavf_get_per_queue_coalesce,
	.set_per_queue_coalesce = iavf_set_per_queue_coalesce,
#endif
#ifdef ETHTOOL_GRXRINGS
	.set_rxnfc		= iavf_set_rxnfc,
	.get_rxnfc		= iavf_get_rxnfc,
#endif
#ifndef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
#if defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH)
	.get_rxfh_indir_size	= iavf_get_rxfh_indir_size,
	.get_rxfh		= iavf_get_rxfh,
	.set_rxfh		= iavf_set_rxfh,
#endif /* ETHTOOL_GRSSH && ETHTOOL_SRSSH */
#ifdef ETHTOOL_GCHANNELS
	.get_channels		= iavf_get_channels,
	.set_channels		= iavf_set_channels,
#endif
#endif /* HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT */
#if defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH)
	.get_rxfh_key_size	= iavf_get_rxfh_key_size,
#endif /* ETHTOOL_GRSSH && ETHTOOL_SRSSH */
#ifdef ETHTOOL_GLINKSETTINGS
	.get_link_ksettings	= iavf_get_link_ksettings,
#else
	.get_settings		= iavf_get_settings,
#endif /* ETHTOOL_GLINKSETTINGS */
#ifdef HAVE_ETHTOOL_GET_TS_INFO
#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
	.get_ts_info		= iavf_get_ts_info,
#endif
#endif
};

#ifdef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
static const struct ethtool_ops_ext iavf_ethtool_ops_ext = {
	.size			= sizeof(struct ethtool_ops_ext),
	.get_channels		= iavf_get_channels,
	.set_channels		= iavf_set_channels,
#if defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH)
	.get_rxfh_key_size	= iavf_get_rxfh_key_size,
	.get_rxfh_indir_size	= iavf_get_rxfh_indir_size,
	.get_rxfh		= iavf_get_rxfh,
	.set_rxfh		= iavf_set_rxfh,
#endif /* ETHTOOL_GRSSH && ETHTOOL_SRSSH */
};
#endif /* HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT */

/**
 * iavf_set_ethtool_ops - Initialize ethtool ops struct
 * @netdev: network interface device structure
 *
 * Sets ethtool ops struct in our netdev so that ethtool can call
 * our functions.
 **/
void iavf_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &iavf_ethtool_ops;
#ifdef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
	set_ethtool_ops_ext(netdev, &iavf_ethtool_ops_ext);
#endif
}
#endif /* SIOCETHTOOL */
