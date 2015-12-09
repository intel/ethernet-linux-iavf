/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
 * Copyright(c) 2013 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifndef _I40E_PROTOTYPE_H_
#define _I40E_PROTOTYPE_H_

#include "i40e_type.h"
#include "i40e_alloc.h"
#include "i40e_virtchnl.h"

/* Prototypes for shared code functions that are not in
 * the standard function pointer structures.  These are
 * mostly because they are needed even before the init
 * has happened and will assist in the early SW and FW
 * setup.
 */

/* adminq functions */
i40e_status i40e_init_adminq(struct i40e_hw *hw);
i40e_status i40e_shutdown_adminq(struct i40e_hw *hw);
i40e_status i40e_init_asq(struct i40e_hw *hw);
i40e_status i40e_init_arq(struct i40e_hw *hw);
i40e_status i40e_alloc_adminq_asq_ring(struct i40e_hw *hw);
i40e_status i40e_alloc_adminq_arq_ring(struct i40e_hw *hw);
i40e_status i40e_shutdown_asq(struct i40e_hw *hw);
i40e_status i40e_shutdown_arq(struct i40e_hw *hw);
u16 i40e_clean_asq(struct i40e_hw *hw);
void i40e_free_adminq_asq(struct i40e_hw *hw);
void i40e_free_adminq_arq(struct i40e_hw *hw);
i40e_status i40e_validate_mac_addr(u8 *mac_addr);
void i40e_adminq_init_ring_data(struct i40e_hw *hw);
i40e_status i40e_clean_arq_element(struct i40e_hw *hw,
					     struct i40e_arq_event_info *e,
					     u16 *events_pending);
i40e_status i40e_asq_send_command(struct i40e_hw *hw,
				struct i40e_aq_desc *desc,
				void *buff, /* can be NULL */
				u16  buff_size,
				struct i40e_asq_cmd_details *cmd_details);
bool i40e_asq_done(struct i40e_hw *hw);

/* debug function for adminq */
void i40e_debug_aq(struct i40e_hw *hw, enum i40e_debug_mask mask,
		   void *desc, void *buffer, u16 buf_len);

void i40e_idle_aq(struct i40e_hw *hw);
void i40e_resume_aq(struct i40e_hw *hw);
bool i40e_check_asq_alive(struct i40e_hw *hw);
i40e_status i40e_aq_queue_shutdown(struct i40e_hw *hw, bool unloading);
const char *i40e_aq_str(struct i40e_hw *hw, enum i40e_admin_queue_err aq_err);
const char *i40e_stat_str(struct i40e_hw *hw, i40e_status stat_err);

i40e_status i40e_set_mac_type(struct i40e_hw *hw);

extern struct i40e_rx_ptype_decoded i40e_ptype_lookup[];

static INLINE struct i40e_rx_ptype_decoded decode_rx_desc_ptype(u8 ptype)
{
	return i40e_ptype_lookup[ptype];
}

/* prototype for functions used for SW spinlocks */
void i40e_init_spinlock(struct i40e_spinlock *sp);
void i40e_acquire_spinlock(struct i40e_spinlock *sp);
void i40e_release_spinlock(struct i40e_spinlock *sp);
void i40e_destroy_spinlock(struct i40e_spinlock *sp);

/* i40e_common for VF drivers*/
void i40e_vf_parse_hw_config(struct i40e_hw *hw,
			     struct i40e_virtchnl_vf_resource *msg);
i40e_status i40e_vf_reset(struct i40e_hw *hw);
i40e_status i40e_aq_send_msg_to_pf(struct i40e_hw *hw,
				enum i40e_virtchnl_ops v_opcode,
				i40e_status v_retval,
				u8 *msg, u16 msglen,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_set_filter_control(struct i40e_hw *hw,
				struct i40e_filter_control_settings *settings);
i40e_status i40e_aq_add_rem_control_packet_filter(struct i40e_hw *hw,
				u8 *mac_addr, u16 ethtype, u16 flags,
				u16 vsi_seid, u16 queue, bool is_add,
				struct i40e_control_filter_stats *stats,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_debug_dump(struct i40e_hw *hw, u8 cluster_id,
				u8 table_id, u32 start_index, u16 buff_size,
				void *buff, u16 *ret_buff_size,
				u8 *ret_next_table, u32 *ret_next_index,
				struct i40e_asq_cmd_details *cmd_details);
void i40e_add_filter_to_drop_tx_flow_control_frames(struct i40e_hw *hw,
						    u16 vsi_seid);
#endif /* _I40E_PROTOTYPE_H_ */