/*
 * field.cpp
 *
 *  Created on: 2010-7-21
 *      Author: Argon
 */

#include "field.h"
#include "duel.h"
#include "card.h"
#include "group.h"
#include "effect.h"
#include "interpreter.h"
#include <iostream>
#include <cstring>
#include <map>

int32 field::field_used_count[32] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5};

bool chain::chain_operation_sort(const chain& c1, const chain& c2) {
	return c1.triggering_effect->id < c2.triggering_effect->id;
}
bool tevent::operator< (const tevent& v) const {
	return memcmp(this, &v, sizeof(tevent)) < 0;
}
field::field(duel* pduel) {
	this->pduel = pduel;
	infos.field_id = 1;
	infos.copy_id = 1;
	infos.can_shuffle = TRUE;
	infos.turn_id = 0;
	infos.card_id = 1;
	infos.phase = 0;
	infos.turn_player = 0;
	for (int i = 0; i < 2; ++i) {
		cost[i].count = 0;
		cost[i].amount = 0;
		core.hint_timing[i] = 0;
		player[i].lp = 8000;
		player[i].start_count = 5;
		player[i].draw_count = 1;
		player[i].disabled_location = 0;
		player[i].used_location = 0;
		player[i].extra_p_count = 0;
		player[i].tag_extra_p_count = 0;
		player[i].list_mzone.reserve(5);
		player[i].list_szone.reserve(8);
		player[i].list_main.reserve(45);
		player[i].list_hand.reserve(10);
		player[i].list_grave.reserve(30);
		player[i].list_remove.reserve(30);
		player[i].list_extra.reserve(15);
		for(int j = 0; j < 5; ++j)
			player[i].list_mzone.push_back(0);
		for(int j = 0; j < 8; ++j)
			player[i].list_szone.push_back(0);
		core.shuffle_deck_check[i] = FALSE;
		core.shuffle_hand_check[i] = FALSE;
	}
	core.pre_field[0] = 0;
	core.pre_field[1] = 0;
	for (int i = 0; i < 5; ++i)
		core.opp_mzone[i] = 0;
	core.summoning_card = 0;
	core.summon_depth = 0;
	core.summon_cancelable = FALSE;
	core.chain_limit = 0;
	core.chain_limit_p = 0;
	core.chain_solving = FALSE;
	core.conti_solving = FALSE;
	core.conti_player = PLAYER_NONE;
	core.win_player = 5;
	core.win_reason = 0;
	core.reason_effect = 0;
	core.reason_player = PLAYER_NONE;
	core.selfdes_disabled = FALSE;
	core.flip_delayed = FALSE;
	core.overdraw[0] = FALSE;
	core.overdraw[1] = FALSE;
	core.check_level = 0;
	core.limit_tuner = 0;
	core.limit_syn = 0;
	core.limit_xyz = 0;
	core.limit_xyz_minc = 0;
	core.limit_xyz_maxc = 0;
	core.duel_options = 0;
	core.attacker = 0;
	core.attack_target = 0;
	core.attack_rollback = FALSE;
	core.deck_reversed = FALSE;
	core.remove_brainwashing = FALSE;
	core.update_field = FALSE;
	core.effect_damage_step = FALSE;
	core.shuffle_check_disabled = FALSE;
	core.global_flag = 0;
	nil_event.event_code = 0;
	nil_event.event_cards = 0;
	nil_event.event_player = PLAYER_NONE;
	nil_event.event_value = 0;
	nil_event.reason = 0;
	nil_event.reason_effect = 0;
	nil_event.reason_player = PLAYER_NONE;
}
field::~field() {

}
void field::reload_field_info() {
	pduel->write_buffer8(MSG_RELOAD_FIELD);
	for(int playerid = 0; playerid < 2; ++playerid) {
		pduel->write_buffer32(player[playerid].lp);
		for(auto& pcard : player[playerid].list_mzone) {
			if(pcard) {
				pduel->write_buffer8(1);
				pduel->write_buffer8(pcard->current.position);
				pduel->write_buffer8(pcard->xyz_materials.size());
			} else {
				pduel->write_buffer8(0);
			}
		}
		for(auto& pcard : player[playerid].list_szone) {
			if(pcard) {
				pduel->write_buffer8(1);
				pduel->write_buffer8(pcard->current.position);
			} else {
				pduel->write_buffer8(0);
			}
		}
		pduel->write_buffer8(player[playerid].list_main.size());
		pduel->write_buffer8(player[playerid].list_hand.size());
		pduel->write_buffer8(player[playerid].list_grave.size());
		pduel->write_buffer8(player[playerid].list_remove.size());
		pduel->write_buffer8(player[playerid].list_extra.size());
		pduel->write_buffer8(player[playerid].extra_p_count);
	}
	pduel->write_buffer8(core.current_chain.size());
	for(auto& chit : core.current_chain) {
		effect* peffect = chit.triggering_effect;
		pduel->write_buffer32(peffect->handler->data.code);
		pduel->write_buffer32(peffect->handler->get_info_location());
		pduel->write_buffer8(chit.triggering_controler);
		pduel->write_buffer8(chit.triggering_location);
		pduel->write_buffer8(chit.triggering_sequence);
		pduel->write_buffer32(peffect->description);
	}
}
// Debug.AddCard() will call this function directly
// check Fusion/S/X monster redirection by the rule
void field::add_card(uint8 playerid, card* pcard, uint8 location, uint8 sequence) {
	if (pcard->current.location != 0)
		return;
	if (!is_location_useable(playerid, location, sequence))
		return;
	if((pcard->data.type & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ)) && (location & (LOCATION_HAND | LOCATION_DECK))) {
		location = LOCATION_EXTRA;
		pcard->operation_param = (pcard->operation_param & 0x00ffffff) | (POS_FACEDOWN_DEFENCE << 24);
	}
	pcard->current.controler = playerid;
	pcard->current.location = location;
	switch (location) {
	case LOCATION_MZONE: {
		player[playerid].list_mzone[sequence] = pcard;
		pcard->current.sequence = sequence;
		break;
	}
	case LOCATION_SZONE: {
		player[playerid].list_szone[sequence] = pcard;
		pcard->current.sequence = sequence;
		break;
	}
	case LOCATION_DECK: {
		if (sequence == 0) {		//deck top
			player[playerid].list_main.push_back(pcard);
			pcard->current.sequence = player[playerid].list_main.size() - 1;
		} else if (sequence == 1) {		//deck button
			player[playerid].list_main.insert(player[playerid].list_main.begin(), pcard);
			reset_sequence(playerid, LOCATION_DECK);
		} else {		//deck top & shuffle
			player[playerid].list_main.push_back(pcard);
			pcard->current.sequence = player[playerid].list_main.size() - 1;
			if(!core.shuffle_check_disabled)
				core.shuffle_deck_check[playerid] = TRUE;
		}
		pcard->operation_param = (pcard->operation_param & 0x00ffffff) | (POS_FACEDOWN << 24);
		break;
	}
	case LOCATION_HAND: {
		player[playerid].list_hand.push_back(pcard);
		pcard->current.sequence = player[playerid].list_hand.size() - 1;
		uint32 pos = pcard->is_affected_by_effect(EFFECT_PUBLIC) ? POS_FACEUP : POS_FACEDOWN;
		pcard->operation_param = (pcard->operation_param & 0x00ffffff) | (pos << 24);
		if(!(pcard->current.reason & REASON_DRAW) && !core.shuffle_check_disabled)
			core.shuffle_hand_check[playerid] = TRUE;
		break;
	}
	case LOCATION_GRAVE: {
		player[playerid].list_grave.push_back(pcard);
		pcard->current.sequence = player[playerid].list_grave.size() - 1;
		break;
	}
	case LOCATION_REMOVED: {
		player[playerid].list_remove.push_back(pcard);
		pcard->current.sequence = player[playerid].list_remove.size() - 1;
		break;
	}
	case LOCATION_EXTRA: {
		player[playerid].list_extra.push_back(pcard);
		pcard->current.sequence = player[playerid].list_extra.size() - 1;
		if((pcard->data.type & TYPE_PENDULUM) && ((pcard->operation_param >> 24) & POS_FACEUP))
			++player[playerid].extra_p_count;
		break;
	}
	}
	pcard->apply_field_effect();
	pcard->fieldid = infos.field_id++;
	pcard->fieldid_r = pcard->fieldid;
	pcard->turnid = infos.turn_id;
	if (location == LOCATION_MZONE)
		player[playerid].used_location |= 1 << sequence;
	if (location == LOCATION_SZONE)
		player[playerid].used_location |= 256 << sequence;
}
void field::remove_card(card* pcard) {
	if (pcard->current.controler == PLAYER_NONE || pcard->current.location == 0)
		return;
	uint8 playerid = pcard->current.controler;
	switch (pcard->current.location) {
	case LOCATION_MZONE:
		player[playerid].list_mzone[pcard->current.sequence] = 0;
		break;
	case LOCATION_SZONE:
		player[playerid].list_szone[pcard->current.sequence] = 0;
		break;
	case LOCATION_DECK:
		player[playerid].list_main.erase(player[playerid].list_main.begin() + pcard->current.sequence);
		reset_sequence(playerid, LOCATION_DECK);
		if(!core.shuffle_check_disabled)
			core.shuffle_deck_check[playerid] = TRUE;
		break;
	case LOCATION_HAND:
		player[playerid].list_hand.erase(player[playerid].list_hand.begin() + pcard->current.sequence);
		reset_sequence(playerid, LOCATION_HAND);
		break;
	case LOCATION_GRAVE:
		player[playerid].list_grave.erase(player[playerid].list_grave.begin() + pcard->current.sequence);
		reset_sequence(playerid, LOCATION_GRAVE);
		break;
	case LOCATION_REMOVED:
		player[playerid].list_remove.erase(player[playerid].list_remove.begin() + pcard->current.sequence);
		reset_sequence(playerid, LOCATION_REMOVED);
		break;
	case LOCATION_EXTRA:
		player[playerid].list_extra.erase(player[playerid].list_extra.begin() + pcard->current.sequence);
		reset_sequence(playerid, LOCATION_EXTRA);
		if((pcard->data.type & TYPE_PENDULUM) && (pcard->current.position & POS_FACEUP))
			--player[playerid].extra_p_count;
		break;
	}
	pcard->cancel_field_effect();
	if (pcard->current.location == LOCATION_MZONE)
		player[playerid].used_location &= ~(1 << pcard->current.sequence);
	if (pcard->current.location == LOCATION_SZONE)
		player[playerid].used_location &= ~(256 << pcard->current.sequence);
	pcard->previous.controler = pcard->current.controler;
	pcard->previous.location = pcard->current.location;
	pcard->previous.sequence = pcard->current.sequence;
	pcard->previous.position = pcard->current.position;
	pcard->current.controler = PLAYER_NONE;
	pcard->current.location = 0;
	pcard->current.sequence = 0;
}
// check Fusion/S/X monster redirection by the rule
// it will call remove_card(), add_card()
void field::move_card(uint8 playerid, card* pcard, uint8 location, uint8 sequence) {
	if (!is_location_useable(playerid, location, sequence))
		return;
	uint8 preplayer = pcard->current.controler;
	uint8 presequence = pcard->current.sequence;
	if((pcard->data.type & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ)) && (location & (LOCATION_HAND | LOCATION_DECK))) {
		location = LOCATION_EXTRA;
		pcard->operation_param = (pcard->operation_param & 0x00ffffff) | (POS_FACEDOWN_DEFENCE << 24);
	}
	if (pcard->current.location) {
		if (pcard->current.location == location) {
			if (pcard->current.location == LOCATION_DECK) {
				if(preplayer == playerid) {
					pduel->write_buffer8(MSG_MOVE);
					pduel->write_buffer32(pcard->data.code);
					pduel->write_buffer32(pcard->get_info_location());
					player[preplayer].list_main.erase(player[preplayer].list_main.begin() + pcard->current.sequence);
					if (sequence == 0) {		//deck top
						player[playerid].list_main.push_back(pcard);
					} else if (sequence == 1) {
						player[playerid].list_main.insert(player[playerid].list_main.begin(), pcard);
					} else {
						player[playerid].list_main.push_back(pcard);
						if(!core.shuffle_check_disabled)
							core.shuffle_deck_check[playerid] = true;
					}
					reset_sequence(playerid, LOCATION_DECK);
					pcard->previous.controler = preplayer;
					pcard->current.controler = playerid;
					pduel->write_buffer32(pcard->get_info_location());
					pduel->write_buffer32(pcard->current.reason);
					return;
				} else
					remove_card(pcard);
			} else if(location & LOCATION_ONFIELD) {
				if (playerid == preplayer && sequence == presequence)
					return;
				if((location == LOCATION_MZONE && (sequence < 0 || sequence > 4 || player[playerid].list_mzone[sequence]))
				        || (location == LOCATION_SZONE && (sequence < 0 || sequence > 4 || player[playerid].list_szone[sequence])))
					return;
				if(preplayer == playerid) {
					pduel->write_buffer8(MSG_MOVE);
					pduel->write_buffer32(pcard->data.code);
					pduel->write_buffer32(pcard->get_info_location());
				}
				pcard->previous.controler = pcard->current.controler;
				pcard->previous.location = pcard->current.location;
				pcard->previous.sequence = pcard->current.sequence;
				pcard->previous.position = pcard->current.position;
				if (location == LOCATION_MZONE) {
					player[preplayer].list_mzone[presequence] = 0;
					player[preplayer].used_location &= ~(1 << presequence);
					player[playerid].list_mzone[sequence] = pcard;
					player[playerid].used_location |= 1 << sequence;
					pcard->current.controler = playerid;
					pcard->current.sequence = sequence;
				} else {
					player[preplayer].list_szone[presequence] = 0;
					player[preplayer].used_location &= ~(256 << presequence);
					player[playerid].list_szone[sequence] = pcard;
					player[playerid].used_location |= 256 << sequence;
					pcard->current.controler = playerid;
					pcard->current.sequence = sequence;
				}
				if(preplayer == playerid) {
					pduel->write_buffer32(pcard->get_info_location());
					pduel->write_buffer32(pcard->current.reason);
				}
				return;
			} else if(location == LOCATION_HAND) {
				if(preplayer == playerid)
					return;
				remove_card(pcard);
			} else {
				if(location == LOCATION_GRAVE) {
					if(pcard->current.sequence == player[pcard->current.controler].list_grave.size() - 1)
						return;
					pduel->write_buffer8(MSG_MOVE);
					pduel->write_buffer32(pcard->data.code);
					pduel->write_buffer32(pcard->get_info_location());
					player[pcard->current.controler].list_grave.erase(player[pcard->current.controler].list_grave.begin() + pcard->current.sequence);
					player[pcard->current.controler].list_grave.push_back(pcard);
					reset_sequence(pcard->current.controler, LOCATION_GRAVE);
					pduel->write_buffer32(pcard->get_info_location());
					pduel->write_buffer32(pcard->current.reason);
				} else if(location == LOCATION_REMOVED) {
					if(pcard->current.sequence == player[pcard->current.controler].list_remove.size() - 1)
						return;
					pduel->write_buffer8(MSG_MOVE);
					pduel->write_buffer32(pcard->data.code);
					pduel->write_buffer32(pcard->get_info_location());
					player[pcard->current.controler].list_remove.erase(player[pcard->current.controler].list_remove.begin() + pcard->current.sequence);
					player[pcard->current.controler].list_remove.push_back(pcard);
					reset_sequence(pcard->current.controler, LOCATION_REMOVED);
					pduel->write_buffer32(pcard->get_info_location());
					pduel->write_buffer32(pcard->current.reason);
				} else {
					pduel->write_buffer8(MSG_MOVE);
					pduel->write_buffer32(pcard->data.code);
					pduel->write_buffer32(pcard->get_info_location());
					player[pcard->current.controler].list_extra.erase(player[pcard->current.controler].list_extra.begin() + pcard->current.sequence);
					player[pcard->current.controler].list_extra.push_back(pcard);
					reset_sequence(pcard->current.controler, LOCATION_EXTRA);
					pduel->write_buffer32(pcard->get_info_location());
					pduel->write_buffer32(pcard->current.reason);
				}
				return;
			}
		} else {
			if((pcard->data.type & TYPE_PENDULUM) && (location == LOCATION_GRAVE)
			        && pcard->is_capable_send_to_extra(playerid)
			        && (((pcard->current.location == LOCATION_MZONE) && !pcard->is_status(STATUS_SUMMON_DISABLED))
			        || ((pcard->current.location == LOCATION_SZONE) && !pcard->is_status(STATUS_ACTIVATE_DISABLED)))) {
				location = LOCATION_EXTRA;
				pcard->operation_param = (pcard->operation_param & 0x00ffffff) | (POS_FACEUP_DEFENCE << 24);
			}
			remove_card(pcard);
		}
	}
	add_card(playerid, pcard, location, sequence);
}
// add EFFECT_SET_CONTROL
void field::set_control(card* pcard, uint8 playerid, uint16 reset_phase, uint8 reset_count) {
	if((core.remove_brainwashing && pcard->is_affected_by_effect(EFFECT_REMOVE_BRAINWASHING)) || pcard->refresh_control_status() == playerid)
		return;
	effect* peffect = pduel->new_effect();
	if(core.reason_effect)
		peffect->owner = core.reason_effect->handler;
	else
		peffect->owner = pcard;
	peffect->handler = pcard;
	peffect->type = EFFECT_TYPE_SINGLE;
	peffect->code = EFFECT_SET_CONTROL;
	peffect->value = playerid;
	peffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE;
	peffect->reset_flag = RESET_EVENT | 0xc6c0000;
	if(reset_count) {
		peffect->reset_flag |= RESET_PHASE | reset_phase;
		if(!(peffect->reset_flag & (RESET_SELF_TURN | RESET_OPPO_TURN)))
			peffect->reset_flag |= (RESET_SELF_TURN | RESET_OPPO_TURN);
		peffect->reset_count |= reset_count & 0xff;
	}
	pcard->add_effect(peffect);
	pcard->current.controler = playerid;
}

card* field::get_field_card(uint8 playerid, uint8 location, uint8 sequence) {
	switch(location) {
	case LOCATION_MZONE: {
		if(sequence < 5)
			return player[playerid].list_mzone[sequence];
		else
			return 0;
		break;
	}
	case LOCATION_SZONE: {
		if(sequence < 8)
			return player[playerid].list_szone[sequence];
		else
			return 0;
		break;
	}
	case LOCATION_DECK: {
		if(sequence < player[playerid].list_main.size())
			return player[playerid].list_main[sequence];
		else
			return 0;
		break;
	}
	case LOCATION_HAND: {
		if(sequence < player[playerid].list_hand.size())
			return player[playerid].list_hand[sequence];
		else
			return 0;
		break;
	}
	case LOCATION_GRAVE: {
		if(sequence < player[playerid].list_grave.size())
			return player[playerid].list_grave[sequence];
		else
			return 0;
		break;
	}
	case LOCATION_REMOVED: {
		if(sequence < player[playerid].list_remove.size())
			return player[playerid].list_remove[sequence];
		else
			return 0;
		break;
	}
	case LOCATION_EXTRA: {
		if(sequence < player[playerid].list_extra.size())
			return player[playerid].list_extra[sequence];
		else
			return 0;
		break;
	}
	}
	return 0;
}
int32 field::is_location_useable(uint8 playerid, uint8 location, uint8 sequence) {
	if (location != LOCATION_MZONE && location != LOCATION_SZONE)
		return TRUE;
	int32 flag = player[playerid].disabled_location | player[playerid].used_location;
	if (location == LOCATION_MZONE && flag & (1 << sequence))
		return FALSE;
	if (location == LOCATION_SZONE && flag & (256 << sequence))
		return FALSE;
	return TRUE;
}
int32 field::get_useable_count(uint8 playerid, uint8 location, uint8 uplayer, uint32 reason, uint32* list) {
	if (location != LOCATION_MZONE && location != LOCATION_SZONE)
		return 0;
	uint32 flag = player[playerid].disabled_location | player[playerid].used_location;
	uint32 used_flag = player[playerid].used_location;
	effect_set eset;
	if (location == LOCATION_MZONE) {
		flag = (flag & 0x1f);
		used_flag = (used_flag & 0x1f);
		if(uplayer < 2)
			filter_player_effect(playerid, EFFECT_MAX_MZONE, &eset);
	} else {
		flag = (flag & 0x1f00) >> 8;
		used_flag = (used_flag & 0x1f00) >> 8;
		if(uplayer < 2)
			filter_player_effect(playerid, EFFECT_MAX_SZONE, &eset);
	}
	if(list)
		*list = flag;
	if(eset.size()) {
		int32 max = 5;
		for (int32 i = 0; i < eset.size(); ++i) {
			pduel->lua->add_param(playerid, PARAM_TYPE_INT);
			pduel->lua->add_param(uplayer, PARAM_TYPE_INT);
			pduel->lua->add_param(reason, PARAM_TYPE_INT);
			int32 v = eset[i]->get_value(3);
			if (max > v)
				max = v;
		}
		int32 block = 5 - field_used_count[flag];
		int32 limit = max - field_used_count[used_flag];
		return block < limit ? block : limit;
	} else {
		return 5 - field_used_count[flag];
	}
}
void field::shuffle(uint8 playerid, uint8 location) {
	if(!(location & (LOCATION_HAND | LOCATION_DECK)))
		return;
	card_vector& svector = (location == LOCATION_HAND) ? player[playerid].list_hand : player[playerid].list_main;
	if(svector.size() == 0)
		return;
	if(location == LOCATION_HAND) {
		bool shuffle = false;
		for(auto& cit : svector)
			if(!cit->is_position(POS_FACEUP))
				shuffle = true;
		if(!shuffle) {
			core.shuffle_hand_check[playerid] = FALSE;
			return;
		}
	}
	if(location == LOCATION_HAND || !(core.duel_options & DUEL_PSEUDO_SHUFFLE)) {
		if(svector.size() > 1) {
			uint32 i = 0, s = svector.size(), r;
			for(i = 0; i < s - 1; ++i) {
				r = pduel->get_next_integer(i, s - 1);
				card* t = svector[i];
				svector[i] = svector[r];
				svector[r] = t;
			}
			reset_sequence(playerid, location);
		}
	}
	if(location == LOCATION_HAND) {
		pduel->write_buffer8(MSG_SHUFFLE_HAND);
		pduel->write_buffer8(playerid);
		for(auto& cit : svector)
			pduel->write_buffer32(cit->data.code);
		core.shuffle_hand_check[playerid] = FALSE;
		for(auto& cit : player[playerid].list_hand)
			cit->update_infos_nocache(0x3fff);
	} else {
		pduel->write_buffer8(MSG_SHUFFLE_DECK);
		pduel->write_buffer8(playerid);
		core.shuffle_deck_check[playerid] = FALSE;
        card* ptop = svector.back();
		if((core.global_flag & GLOBALFLAG_DECK_REVERSE_CHECK) && (core.deck_reversed || (ptop->current.position == POS_FACEUP_DEFENCE))) {
            if(ptop->current.position != POS_FACEUP_DEFENCE)
                pduel->write_buffer32(ptop->data.code);
            else
                pduel->write_buffer32(ptop->data.code | 0x80000000);
		} else
            pduel->write_buffer32(0);
	}
}
void field::reset_sequence(uint8 playerid, uint8 location) {
	if(location & (LOCATION_ONFIELD))
		return;
	uint32 i = 0;
	switch(location) {
	case LOCATION_DECK:
		for(auto& cit : player[playerid].list_main)
			cit->current.sequence = i++;
		break;
	case LOCATION_HAND:
		for(auto cit : player[playerid].list_hand)
			cit->current.sequence = i++;
		break;
	case LOCATION_EXTRA:
		for(auto& cit : player[playerid].list_extra)
			cit->current.sequence = i++;
		break;
	case LOCATION_GRAVE:
		for(auto& cit : player[playerid].list_grave)
			cit->current.sequence = i;
		break;
	case LOCATION_REMOVED:
		for(auto& cit : player[playerid].list_remove)
			cit->current.sequence = i;
		break;
	}
}
void field::swap_deck_and_grave(uint8 playerid) {
	for(auto& clit : player[playerid].list_grave) {
		clit->previous.location = LOCATION_GRAVE;
		clit->previous.sequence = clit->current.sequence;
		clit->enable_field_effect(false);
		clit->cancel_field_effect();
	}
	for(auto& clit : player[playerid].list_main) {
		clit->previous.location = LOCATION_DECK;
		clit->previous.sequence = clit->current.sequence;
		clit->enable_field_effect(false);
		clit->cancel_field_effect();
	}
	player[playerid].list_grave.swap(player[playerid].list_main);
	card_vector ex;
	for(auto clit = player[playerid].list_main.begin(); clit != player[playerid].list_main.end(); ) {
		if((*clit)->data.type & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ)) {
			ex.push_back(*clit);
			clit = player[playerid].list_main.erase(clit);
		} else
			++clit;
	}
	for(auto clit = player[playerid].list_grave.begin(); clit != player[playerid].list_grave.end(); ++clit) {
		(*clit)->current.location = LOCATION_GRAVE;
		(*clit)->current.reason = REASON_EFFECT;
		(*clit)->current.reason_effect = core.reason_effect;
		(*clit)->current.reason_player = core.reason_player;
		(*clit)->apply_field_effect();
		(*clit)->enable_field_effect(true);
		(*clit)->reset(RESET_TOGRAVE, RESET_EVENT);
	}
	for(auto clit = player[playerid].list_main.begin(); clit != player[playerid].list_main.end(); ++clit) {
		(*clit)->current.location = LOCATION_DECK;
		(*clit)->current.reason = REASON_EFFECT;
		(*clit)->current.reason_effect = core.reason_effect;
		(*clit)->current.reason_player = core.reason_player;
		(*clit)->apply_field_effect();
		(*clit)->enable_field_effect(true);
		(*clit)->reset(RESET_TODECK, RESET_EVENT);
	}
	for(auto clit = ex.begin(); clit != ex.end(); ++clit) {
		(*clit)->current.location = LOCATION_EXTRA;
		(*clit)->current.reason = REASON_EFFECT;
		(*clit)->current.reason_effect = core.reason_effect;
		(*clit)->current.reason_player = core.reason_player;
		(*clit)->apply_field_effect();
		(*clit)->enable_field_effect(true);
		(*clit)->reset(RESET_TODECK, RESET_EVENT);
	}
	player[playerid].list_extra.insert(player[playerid].list_extra.end(), ex.begin(), ex.end());
	reset_sequence(playerid, LOCATION_GRAVE);
	reset_sequence(playerid, LOCATION_EXTRA);
	pduel->write_buffer8(MSG_SWAP_GRAVE_DECK);
	pduel->write_buffer8(playerid);
	shuffle(playerid, LOCATION_DECK);
}
void field::reverse_deck(uint8 playerid) {
	int32 count = player[playerid].list_main.size();
	if(count == 0)
		return;
	for(int i = 0; i < count / 2; ++i) {
		card* tmp = player[playerid].list_main[i];
		tmp->current.sequence = count - 1 - i;
		player[playerid].list_main[count - 1 - i]->current.sequence = i;
		player[playerid].list_main[i] = player[playerid].list_main[count - 1 - i];
		player[playerid].list_main[count - 1 - i] = tmp;
	}
}
void field::tag_swap(uint8 playerid) {
	//main
	for(auto& clit : player[playerid].list_main) {
		clit->enable_field_effect(false);
		clit->cancel_field_effect();
	}
	std::swap(player[playerid].list_main, player[playerid].tag_list_main);
	for(auto clit : player[playerid].list_main) {
		clit->apply_field_effect();
		clit->enable_field_effect(true);
	}
	//hand
	for(auto& clit : player[playerid].list_hand) {
		clit->enable_field_effect(false);
		clit->cancel_field_effect();
	}
	std::swap(player[playerid].list_hand, player[playerid].tag_list_hand);
	for(auto& clit : player[playerid].list_hand) {
		clit->apply_field_effect();
		clit->enable_field_effect(true);
	}
	//extra
	for(auto& clit : player[playerid].list_extra) {
		clit->enable_field_effect(false);
		clit->cancel_field_effect();
	}
	std::swap(player[playerid].list_extra, player[playerid].tag_list_extra);
    std::swap(player[playerid].extra_p_count, player[playerid].tag_extra_p_count);
	for(auto& clit : player[playerid].list_extra) {
		clit->apply_field_effect();
		clit->enable_field_effect(true);
	}
	pduel->write_buffer8(MSG_TAG_SWAP);
	pduel->write_buffer8(playerid);
	pduel->write_buffer8(player[playerid].list_main.size());
	pduel->write_buffer8(player[playerid].list_extra.size());
	pduel->write_buffer8(player[playerid].extra_p_count);
	pduel->write_buffer8(player[playerid].list_hand.size());
	if(core.deck_reversed && player[playerid].list_main.size())
		pduel->write_buffer32(player[playerid].list_main.back()->data.code);
	else
		pduel->write_buffer32(0);
    for(auto cit : player[playerid].list_hand) {
		pduel->write_buffer32(cit->data.code | (cit->is_position(POS_FACEUP) ? 0x80000000 : 0));
        cit->update_infos_nocache(0x3ffe);
    }
    for(auto cit : player[playerid].list_extra)
        pduel->write_buffer32(cit->data.code | (cit->is_position(POS_FACEUP) ? 0x80000000 : 0));
}
void field::add_effect(effect* peffect, uint8 owner_player) {
	if (!peffect->handler) {
		peffect->flag[0] |= EFFECT_FLAG_FIELD_ONLY;
		peffect->handler = peffect->owner;
		peffect->effect_owner = owner_player;
		peffect->id = infos.field_id++;
	}
	if((peffect->type & 0x7e0)
		|| (core.reason_effect && (core.reason_effect->status & EFFECT_STATUS_ACTIVATED)))
		peffect->status |= EFFECT_STATUS_ACTIVATED;
	peffect->card_type = peffect->owner->data.type;
	effect_container::iterator it;
	if (!(peffect->type & EFFECT_TYPE_ACTIONS)) {
		it = effects.aura_effect.insert(std::make_pair(peffect->code, peffect));
		if(peffect->code == EFFECT_SPSUMMON_COUNT_LIMIT)
			effects.spsummon_count_eff.insert(peffect);
	} else {
		if (peffect->type & EFFECT_TYPE_IGNITION)
			it = effects.ignition_effect.insert(std::make_pair(peffect->code, peffect));
		else if (peffect->type & EFFECT_TYPE_ACTIVATE)
			it = effects.activate_effect.insert(std::make_pair(peffect->code, peffect));
		else if (peffect->type & EFFECT_TYPE_TRIGGER_O && peffect->type & EFFECT_TYPE_FIELD)
			it = effects.trigger_o_effect.insert(std::make_pair(peffect->code, peffect));
		else if (peffect->type & EFFECT_TYPE_TRIGGER_F && peffect->type & EFFECT_TYPE_FIELD)
			it = effects.trigger_f_effect.insert(std::make_pair(peffect->code, peffect));
		else if (peffect->type & EFFECT_TYPE_QUICK_O)
			it = effects.quick_o_effect.insert(std::make_pair(peffect->code, peffect));
		else if (peffect->type & EFFECT_TYPE_QUICK_F)
			it = effects.quick_f_effect.insert(std::make_pair(peffect->code, peffect));
		else if (peffect->type & EFFECT_TYPE_CONTINUOUS)
			it = effects.continuous_effect.insert(std::make_pair(peffect->code, peffect));
	}
	effects.indexer.insert(std::make_pair(peffect, it));
	if((peffect->is_flag(EFFECT_FLAG_FIELD_ONLY))) {
		if(peffect->is_flag(EFFECT_FLAG_OATH))
			effects.oath.insert(std::make_pair(peffect, core.reason_effect));
		if(peffect->reset_flag & RESET_PHASE)
			effects.pheff.insert(peffect);
		if(peffect->reset_flag & RESET_CHAIN)
			effects.cheff.insert(peffect);
		if(peffect->is_flag(EFFECT_FLAG_COUNT_LIMIT))
			effects.rechargeable.insert(peffect);
	}
}
void field::remove_effect(effect* peffect) {
	auto eit = effects.indexer.find(peffect);
	if (eit == effects.indexer.end())
		return;
	auto it = eit->second;
	if (!(peffect->type & EFFECT_TYPE_ACTIONS)) {
		effects.aura_effect.erase(it);
		if(peffect->code == EFFECT_SPSUMMON_COUNT_LIMIT)
			effects.spsummon_count_eff.erase(peffect);
	} else {
		if (peffect->type & EFFECT_TYPE_IGNITION)
			effects.ignition_effect.erase(it);
		else if (peffect->type & EFFECT_TYPE_ACTIVATE)
			effects.activate_effect.erase(it);
		else if (peffect->type & EFFECT_TYPE_TRIGGER_O)
			effects.trigger_o_effect.erase(it);
		else if (peffect->type & EFFECT_TYPE_TRIGGER_F)
			effects.trigger_f_effect.erase(it);
		else if (peffect->type & EFFECT_TYPE_QUICK_O)
			effects.quick_o_effect.erase(it);
		else if (peffect->type & EFFECT_TYPE_QUICK_F)
			effects.quick_f_effect.erase(it);
		else if (peffect->type & EFFECT_TYPE_CONTINUOUS)
			effects.continuous_effect.erase(it);
	}
	effects.indexer.erase(peffect);
	if((peffect->is_flag(EFFECT_FLAG_FIELD_ONLY))) {
		if(peffect->is_flag(EFFECT_FLAG_OATH))
			effects.oath.erase(peffect);
		if(peffect->reset_flag & RESET_PHASE)
			effects.pheff.erase(peffect);
		if(peffect->reset_flag & RESET_CHAIN)
			effects.cheff.erase(peffect);
		if(peffect->is_flag(EFFECT_FLAG_COUNT_LIMIT))
			effects.rechargeable.erase(peffect);
		core.reseted_effects.insert(peffect);
	}
}
void field::remove_oath_effect(effect* reason_effect) {
	for(auto oeit = effects.oath.begin(); oeit != effects.oath.end();) {
		auto rm = oeit++;
		if(rm->second == reason_effect) {
			effect* peffect = rm->first;
			effects.oath.erase(rm);
			if(peffect->is_flag(EFFECT_FLAG_FIELD_ONLY))
				remove_effect(peffect);
			else
				peffect->handler->remove_effect(peffect);
		}
	}
}
void field::reset_effect(uint32 id, uint32 reset_type) {
	int32 result;
	for (auto it = effects.indexer.begin(); it != effects.indexer.end();) {
		auto rm = it++;
		auto peffect = rm->first;
		auto pit = rm->second;
		if (!(peffect->is_flag(EFFECT_FLAG_FIELD_ONLY)))
			continue;
		result = peffect->reset(id, reset_type);
		if (result) {
			if (!(peffect->type & EFFECT_TYPE_ACTIONS)) {
				if (peffect->is_disable_related())
					update_disable_check_list(peffect);
				effects.aura_effect.erase(pit);
			} else {
				if (peffect->type & EFFECT_TYPE_IGNITION)
					effects.ignition_effect.erase(pit);
				else if (peffect->type & EFFECT_TYPE_ACTIVATE)
					effects.activate_effect.erase(pit);
				else if (peffect->type & EFFECT_TYPE_TRIGGER_O)
					effects.trigger_o_effect.erase(pit);
				else if (peffect->type & EFFECT_TYPE_TRIGGER_F)
					effects.trigger_f_effect.erase(pit);
				else if (peffect->type & EFFECT_TYPE_QUICK_O)
					effects.quick_o_effect.erase(pit);
				else if (peffect->type & EFFECT_TYPE_QUICK_F)
					effects.quick_f_effect.erase(pit);
				else if (peffect->type & EFFECT_TYPE_CONTINUOUS)
					effects.continuous_effect.erase(pit);
			}
			effects.indexer.erase(peffect);
			pduel->delete_effect(peffect);
		}
	}
}
void field::reset_phase(uint32 phase) {
	for(auto eit = effects.pheff.begin(); eit != effects.pheff.end();) {
		auto rm = eit++;
		if((*rm)->reset(phase, RESET_PHASE)) {
			if((*rm)->is_flag(EFFECT_FLAG_FIELD_ONLY))
				remove_effect((*rm));
			else
				(*rm)->handler->remove_effect((*rm));
		}
	}
}
void field::reset_chain() {
	for(auto eit = effects.cheff.begin(); eit != effects.cheff.end();) {
		auto rm = eit++;
		if((*rm)->is_flag(EFFECT_FLAG_FIELD_ONLY))
			remove_effect((*rm));
		else
			(*rm)->handler->remove_effect((*rm));
	}
}
void field::add_effect_code(uint32 code, uint32 playerid) {
	auto& count_map = (code & EFFECT_COUNT_CODE_DUEL) ? core.effect_count_code_duel : core.effect_count_code;
	count_map[code + (playerid << 30)]++;
}
uint32 field::get_effect_code(uint32 code, uint32 playerid) {
	auto& count_map = (code & EFFECT_COUNT_CODE_DUEL) ? core.effect_count_code_duel : core.effect_count_code;
	auto iter = count_map.find(code + (playerid << 30));
	if(iter == count_map.end())
		return 0;
	return iter->second;
}
void field::dec_effect_code(uint32 code, uint32 playerid) {
	auto& count_map = (code & EFFECT_COUNT_CODE_DUEL) ? core.effect_count_code_duel : core.effect_count_code;
	auto iter = count_map.find(code + (playerid << 30));
	if(iter == count_map.end())
		return;
	if(iter->second > 0)
		iter->second--;
}
void field::filter_field_effect(uint32 code, effect_set* eset, uint8 sort) {
	effect* peffect;
	auto rg = effects.aura_effect.equal_range(code);
	for (; rg.first != rg.second; ) {
		peffect = rg.first->second;
		++rg.first;
		if (peffect->is_available())
			eset->add_item(peffect);
	}
	if(sort)
		eset->sort();
}
void field::filter_affected_cards(effect* peffect, card_set* cset) {
	if ((peffect->type & EFFECT_TYPE_ACTIONS) || !(peffect->type & EFFECT_TYPE_FIELD) || (peffect->is_flag(EFFECT_FLAG_PLAYER_TARGET)))
		return;
	uint8 self = peffect->get_handler_player();
	if(self == PLAYER_NONE)
		return;
	uint16 range = peffect->s_range;
	for(uint32 p = 0; p < 2; ++p) {
		if (range & LOCATION_MZONE) {
			for (auto& pcard : player[self].list_mzone) {
				if (pcard && peffect->is_target(pcard))
					cset->insert(pcard);
			}
		}
		if (range & LOCATION_SZONE) {
			for (auto& pcard : player[self].list_szone) {
				if (pcard && peffect->is_target(pcard))
					cset->insert(pcard);
			}
		}
		if (range & LOCATION_GRAVE) {
			for (auto& pcard : player[self].list_grave) {
				if (peffect->is_target(pcard))
					cset->insert(pcard);
			}
		}
		if (range & LOCATION_REMOVED) {
			for (auto& pcard : player[self].list_remove) {
				if (peffect->is_target(pcard))
					cset->insert(pcard);
			}
		}
		if (range & LOCATION_HAND) {
			for (auto& pcard : player[self].list_hand) {
				if (peffect->is_target(pcard))
					cset->insert(pcard);
			}
		}
		range = peffect->o_range;
		self = 1 - self;
	}
}
void field::filter_player_effect(uint8 playerid, uint32 code, effect_set* eset, uint8 sort) {
	auto rg = effects.aura_effect.equal_range(code);
	for (; rg.first != rg.second; ++rg.first) {
		effect* peffect = rg.first->second;
		if (peffect->is_target_player(playerid) && peffect->is_available())
			eset->add_item(peffect);
	}
	if(sort)
		eset->sort();
}
int32 field::filter_matching_card(int32 findex, uint8 self, uint32 location1, uint32 location2, group* pgroup, card* pexception, uint32 extraargs, card** pret, int32 fcount, int32 is_target) {
	if(self != 0 && self != 1)
		return FALSE;
	int32 count = 0;
	uint32 location = location1;
	for(uint32 p = 0; p < 2; ++p) {
		if(location & LOCATION_MZONE) {
			for(uint32 i = 0; i < 5; ++i) {
				auto pcard = player[self].list_mzone[i];
				if(pcard && !pcard->is_status(STATUS_SUMMONING) && !pcard->is_status(STATUS_SUMMON_DISABLED) && !pcard->is_status(STATUS_SPSUMMON_STEP) 
						&& pcard != pexception && pduel->lua->check_matching(pcard, findex, extraargs) 
						&& (!is_target || pcard->is_capable_be_effect_target(core.reason_effect, core.reason_player))) {
					if(pret) {
						*pret = pcard;
						return TRUE;
					}
					count ++;
					if(fcount && count >= fcount)
						return TRUE;
					if(pgroup) {
						pgroup->container.insert(pcard);
					}
				}
			}
		}
		if(location & LOCATION_SZONE) {
			for(auto& pcard : player[self].list_szone) {
				if(pcard && pcard != pexception && pduel->lua->check_matching(pcard, findex, extraargs)
				        && (!is_target || pcard->is_capable_be_effect_target(core.reason_effect, core.reason_player))) {
					if(pret) {
						*pret = pcard;
						return TRUE;
					}
					count ++;
					if(fcount && count >= fcount)
						return TRUE;
					if(pgroup) {
						pgroup->container.insert(pcard);
					}
				}
			}
		}
		if(location & LOCATION_DECK) {
			for(auto cit = player[self].list_main.rbegin(); cit != player[self].list_main.rend(); ++cit) {
				if(*cit != pexception && pduel->lua->check_matching(*cit, findex, extraargs)
				        && (!is_target || (*cit)->is_capable_be_effect_target(core.reason_effect, core.reason_player))) {
					if(pret) {
						*pret = *cit;
						return TRUE;
					}
					count ++;
					if(fcount && count >= fcount)
						return TRUE;
					if(pgroup) {
						pgroup->container.insert(*cit);
					}
				}
			}
		}
		if(location & LOCATION_EXTRA) {
			for(auto cit = player[self].list_extra.rbegin(); cit != player[self].list_extra.rend(); ++cit) {
				if(*cit != pexception && pduel->lua->check_matching(*cit, findex, extraargs)
				        && (!is_target || (*cit)->is_capable_be_effect_target(core.reason_effect, core.reason_player))) {
					if(pret) {
						*pret = *cit;
						return TRUE;
					}
					count ++;
					if(fcount && count >= fcount)
						return TRUE;
					if(pgroup) {
						pgroup->container.insert(*cit);
					}
				}
			}
		}
		if(location & LOCATION_HAND) {
			for(auto cit = player[self].list_hand.begin(); cit != player[self].list_hand.end(); ++cit) {
				if(*cit != pexception && pduel->lua->check_matching(*cit, findex, extraargs)
				        && (!is_target || (*cit)->is_capable_be_effect_target(core.reason_effect, core.reason_player))) {
					if(pret) {
						*pret = *cit;
						return TRUE;
					}
					count ++;
					if(fcount && count >= fcount)
						return TRUE;
					if(pgroup) {
						pgroup->container.insert(*cit);
					}
				}
			}
		}
		if(location & LOCATION_GRAVE) {
			for(auto cit = player[self].list_grave.rbegin(); cit != player[self].list_grave.rend(); ++cit) {
				if(*cit != pexception && pduel->lua->check_matching(*cit, findex, extraargs)
				        && (!is_target || (*cit)->is_capable_be_effect_target(core.reason_effect, core.reason_player))) {
					if(pret) {
						*pret = *cit;
						return TRUE;
					}
					count ++;
					if(fcount && count >= fcount)
						return TRUE;
					if(pgroup) {
						pgroup->container.insert(*cit);
					}
				}
			}
		}
		if(location & LOCATION_REMOVED) {
			for(auto cit = player[self].list_remove.rbegin(); cit != player[self].list_remove.rend(); ++cit) {
				if(*cit != pexception && pduel->lua->check_matching(*cit, findex, extraargs)
				        && (!is_target || (*cit)->is_capable_be_effect_target(core.reason_effect, core.reason_player))) {
					if(pret) {
						*pret = *cit;
						return TRUE;
					}
					count ++;
					if(fcount && count >= fcount)
						return TRUE;
					if(pgroup) {
						pgroup->container.insert(*cit);
					}
				}
			}
		}
		location = location2;
		self = 1 - self;
	}
	return FALSE;
}
int32 field::filter_field_card(uint8 self, uint32 location1, uint32 location2, group* pgroup) {
	if(self != 0 && self != 1)
		return 0;
	uint32 location = location1;
	uint32 count = 0;
	for(uint32 p = 0; p < 2; ++p) {
		if(location & LOCATION_MZONE) {
			for(auto& pcard : player[self].list_mzone) {
				if(pcard && !pcard->is_status(STATUS_SUMMONING)) {
					if(pgroup)
						pgroup->container.insert(pcard);
					count++;
				}
			}
		}
		if(location & LOCATION_SZONE) {
			for(auto& pcard : player[self].list_szone) {
				if(pcard) {
					if(pgroup)
						pgroup->container.insert(pcard);
					count++;
				}
			}
		}
		if(location & LOCATION_HAND) {
			if(pgroup)
				pgroup->container.insert(player[self].list_hand.begin(), player[self].list_hand.end());
			count += player[self].list_hand.size();
		}
		if(location & LOCATION_DECK) {
			if(pgroup)
				pgroup->container.insert(player[self].list_main.rbegin(), player[self].list_main.rend());
			count += player[self].list_main.size();
		}
		if(location & LOCATION_EXTRA) {
			if(pgroup)
				pgroup->container.insert(player[self].list_extra.rbegin(), player[self].list_extra.rend());
			count += player[self].list_extra.size();
		}
		if(location & LOCATION_GRAVE) {
			if(pgroup)
				pgroup->container.insert(player[self].list_grave.rbegin(), player[self].list_grave.rend());
			count += player[self].list_grave.size();
		}
		if(location & LOCATION_REMOVED) {
			if(pgroup)
				pgroup->container.insert(player[self].list_remove.rbegin(), player[self].list_remove.rend());
			count += player[self].list_remove.size();
		}
		location = location2;
		self = 1 - self;
	}
	return count;
}
effect* field::is_player_affected_by_effect(uint8 playerid, uint32 code) {
	auto rg = effects.aura_effect.equal_range(code);
	for (; rg.first != rg.second; ++rg.first) {
		effect* peffect = rg.first->second;
		if (peffect->is_target_player(playerid) && peffect->is_available())
			return peffect;
	}
	return 0;
}
int32 field::get_release_list(uint8 playerid, card_set* release_list, card_set* ex_list, int32 use_con, int32 use_hand, int32 fun, int32 exarg, card* exp) {
	uint32 rcount = 0;
	for(auto& pcard : player[playerid].list_mzone) {
		if(pcard && pcard != exp && pcard->is_releasable_by_nonsummon(playerid)
		        && (!use_con || pduel->lua->check_matching(pcard, fun, exarg))) {
			if(release_list)
				release_list->insert(pcard);
			pcard->operation_param = 1;
			rcount++;
		}
	}
	if(use_hand) {
		for(auto& pcard : player[playerid].list_hand) {
			if(pcard && pcard != exp && pcard->is_releasable_by_nonsummon(playerid)
			        && (!use_con || pduel->lua->check_matching(pcard, fun, exarg))) {
				if(release_list)
					release_list->insert(pcard);
				pcard->operation_param = 1;
				rcount++;
			}
		}
	}
	for(auto& pcard : player[1 - playerid].list_mzone) {
		if(pcard && pcard != exp && (pcard->is_position(POS_FACEUP) || !use_con) && pcard->is_affected_by_effect(EFFECT_EXTRA_RELEASE)
		        && pcard->is_releasable_by_nonsummon(playerid) && (!use_con || pduel->lua->check_matching(pcard, fun, exarg))) {
			if(ex_list)
				ex_list->insert(pcard);
			pcard->operation_param = 1;
			rcount++;
		}
	}
	return rcount;
}
int32 field::check_release_list(uint8 playerid, int32 count, int32 use_con, int32 use_hand, int32 fun, int32 exarg, card* exp) {
	for(auto& pcard : player[playerid].list_mzone) {
		if(pcard && pcard != exp && pcard->is_releasable_by_nonsummon(playerid)
		        && (!use_con || pduel->lua->check_matching(pcard, fun, exarg))) {
			count--;
			if(count == 0)
				return TRUE;
		}
	}
	if(use_hand) {
		for(auto& pcard : player[playerid].list_hand) {
			if(pcard && pcard != exp && pcard->is_releasable_by_nonsummon(playerid)
			        && (!use_con || pduel->lua->check_matching(pcard, fun, exarg))) {
				count--;
				if(count == 0)
					return TRUE;
			}
		}
	}
	for(auto& pcard : player[1 - playerid].list_mzone) {
		if(pcard && pcard != exp && (!use_con || pcard->is_position(POS_FACEUP)) && pcard->is_affected_by_effect(EFFECT_EXTRA_RELEASE)
		        && pcard->is_releasable_by_nonsummon(playerid) && (!use_con || pduel->lua->check_matching(pcard, fun, exarg))) {
			count--;
			if(count == 0)
				return TRUE;
		}
	}
	return FALSE;
}
int32 field::get_summon_release_list(card* target, card_set* release_list, card_set* ex_list, card_set* ex_list_sum, group* mg, uint32 ex) {
	uint8 p = target->current.controler;
	uint32 rcount = 0;
	for(auto& pcard : player[p].list_mzone) {
		if(pcard && pcard->is_releasable_by_summon(p, target)) {
			if(mg && !mg->has_card(pcard))
				continue;
			if(release_list)
				release_list->insert(pcard);
			if(pcard->is_affected_by_effect(EFFECT_DOUBLE_TRIBUTE, target))
				pcard->operation_param = 2;
			else
				pcard->operation_param = 1;
			rcount += pcard->operation_param;
		}
	}
	uint32 ex_sum_max = 0;
    for(auto& pcard : player[1 - p].list_mzone) {
		if(!(pcard && pcard->is_releasable_by_summon(p, target)))
			continue;
		if(mg && !mg->has_card(pcard))
			continue;
		if(ex || pcard->is_affected_by_effect(EFFECT_EXTRA_RELEASE)) {
			if(ex_list)
				ex_list->insert(pcard);
			if(pcard->is_affected_by_effect(EFFECT_DOUBLE_TRIBUTE, target))
				pcard->operation_param = 2;
			else
				pcard->operation_param = 1;
			rcount += pcard->operation_param;
		} else {
			effect* peffect = pcard->is_affected_by_effect(EFFECT_EXTRA_RELEASE_SUM);
			if(!peffect || ((peffect->is_flag(EFFECT_FLAG_COUNT_LIMIT)) && (peffect->reset_count & 0xf00) == 0))
				continue;
			if(ex_list_sum)
				ex_list_sum->insert(pcard);
			if(pcard->is_affected_by_effect(EFFECT_DOUBLE_TRIBUTE, target))
				pcard->operation_param = 2;
			else
				pcard->operation_param = 1;
			if(ex_sum_max < pcard->operation_param)
				ex_sum_max = pcard->operation_param;
		}
	}
	return rcount + ex_sum_max;
}
int32 field::get_summon_count_limit(uint8 playerid) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_SET_SUMMON_COUNT_LIMIT, &eset);
	int32 count = 1, c;
	for(int32 i = 0; i < eset.size(); ++i) {
		c = eset[i]->get_value();
		if(c > count)
			count = c;
	}
	return count;
}
int32 field::get_draw_count(uint8 playerid) {
	effect_set eset;
	filter_player_effect(infos.turn_player, EFFECT_DRAW_COUNT, &eset);
	int32 count = player[playerid].draw_count;
	if(eset.size())
		count = eset.get_last()->get_value();
	return count;
}
void field::get_ritual_material(uint8 playerid, effect* peffect, card_set* material) {
	for(auto& pcard : player[playerid].list_mzone) {
		if(pcard && pcard->get_level() && pcard->is_affect_by_effect(core.reason_effect)
		        && pcard->is_releasable_by_nonsummon(playerid) && pcard->is_releasable_by_effect(playerid, peffect))
			material->insert(pcard);
	}
	for(auto& pcard : player[1 - playerid].list_mzone) {
		if(pcard && pcard->get_level() && pcard->is_affect_by_effect(core.reason_effect)
		        && pcard->is_affected_by_effect(EFFECT_EXTRA_RELEASE)
		        && pcard->is_releasable_by_nonsummon(playerid) && pcard->is_releasable_by_effect(playerid, peffect))
			material->insert(pcard);
	}
	for(auto& pcard : player[playerid].list_hand)
		if((pcard->data.type & TYPE_MONSTER) && pcard->is_releasable_by_nonsummon(playerid))
			material->insert(pcard);
	for(auto& pcard : player[playerid].list_grave)
		if((pcard->data.type & TYPE_MONSTER) && pcard->is_affected_by_effect(EFFECT_EXTRA_RITUAL_MATERIAL) && pcard->is_removeable(playerid))
			material->insert(pcard);
}
void field::ritual_release(card_set* material) {
	card_set rel;
	card_set rem;
	for(auto& pcard : *material) {
		if(pcard->current.location == LOCATION_GRAVE)
			rem.insert(pcard);
		else
			rel.insert(pcard);
	}
	release(&rel, core.reason_effect, REASON_RITUAL + REASON_EFFECT + REASON_MATERIAL, core.reason_player);
	send_to(&rem, core.reason_effect, REASON_RITUAL + REASON_EFFECT + REASON_MATERIAL, core.reason_player, PLAYER_NONE, LOCATION_REMOVED, 0, POS_FACEUP);
}
void field::get_xyz_material(card* scard, int32 findex, uint32 lv, int32 maxc, group* mg) {
	core.xmaterial_lst.clear();
	uint32 xyz_level;
	if(mg) {
        for (auto cit : mg->container) {
			if((xyz_level = cit->check_xyz_level(scard, lv)) && (findex == 0 || pduel->lua->check_matching(cit, findex, 0)))
				core.xmaterial_lst.insert(std::make_pair((xyz_level >> 12) & 0xf, cit));
		}
	} else {
		int32 playerid = scard->current.controler;
		for(int i = 0; i < 5; ++i) {
			card* pcard = player[playerid].list_mzone[i];
			if(pcard && pcard->is_position(POS_FACEUP) && pcard->is_can_be_xyz_material(scard) && (xyz_level = pcard->check_xyz_level(scard, lv))
					&& (findex == 0 || pduel->lua->check_matching(pcard, findex, 0)))
				core.xmaterial_lst.insert(std::make_pair((xyz_level >> 12) & 0xf, pcard));
		}
		for(int i = 0; i < 5; ++i) {
			card* pcard = player[1 - playerid].list_mzone[i];
			if(pcard && pcard->is_position(POS_FACEUP) && pcard->is_can_be_xyz_material(scard) && (xyz_level = pcard->check_xyz_level(scard, lv))
			        && pcard->is_affected_by_effect(EFFECT_XYZ_MATERIAL) && (findex == 0 || pduel->lua->check_matching(pcard, findex, 0)))
				core.xmaterial_lst.insert(std::make_pair((xyz_level >> 12) & 0xf, pcard));
		}
	}
	if(core.global_flag & GLOBALFLAG_XMAT_COUNT_LIMIT) {
		if(maxc > (int32)core.xmaterial_lst.size())
			maxc = (int32)core.xmaterial_lst.size();
		auto iter = core.xmaterial_lst.lower_bound(maxc);
		core.xmaterial_lst.erase(core.xmaterial_lst.begin(), iter);
	}
}
void field::get_overlay_group(uint8 self, uint8 s, uint8 o, card_set* pset) {
	uint8 c = s;
	for(int p = 0; p < 2; ++p) {
		if(c) {
			for(auto& pcard : player[self].list_mzone) {
				if(pcard && !pcard->is_status(STATUS_SUMMONING) && pcard->xyz_materials.size())
					pset->insert(pcard->xyz_materials.begin(), pcard->xyz_materials.end());
			}
		}
		self = 1 - self;
		c = o;
	}
}
int32 field::get_overlay_count(uint8 self, uint8 s, uint8 o) {
	uint8 c = s;
	uint32 count = 0;
	for(int p = 0; p < 2; ++p) {
		if(c) {
			for(auto& pcard : player[self].list_mzone) {
				if(pcard && !pcard->is_status(STATUS_SUMMONING))
					count += pcard->xyz_materials.size();
			}
		}
		self = 1 - self;
		c = o;
	}
	return count;
}
void field::update_disable_check_list(effect* peffect) {
	card_set cset;
	filter_affected_cards(peffect, &cset);
	for (auto it = cset.begin(); it != cset.end(); ++it)
		add_to_disable_check_list(*it);
}
void field::add_to_disable_check_list(card* pcard) {
	if (effects.disable_check_set.find(pcard) != effects.disable_check_set.end())
		return;
	effects.disable_check_set.insert(pcard);
	effects.disable_check_list.push_back(pcard);
}
void field::adjust_disable_check_list() {
	card* checking;
	int32 pre_disable, new_disable;
	if (!effects.disable_check_list.size())
		return;
	card_set checked;
	do {
		checked.clear();
		while (effects.disable_check_list.size()) {
			checking = effects.disable_check_list.front();
			effects.disable_check_list.pop_front();
			effects.disable_check_set.erase(checking);
			checked.insert(checking);
			if (checking->is_status(STATUS_TO_ENABLE + STATUS_TO_DISABLE))
				continue;
			pre_disable = checking->is_status(STATUS_DISABLED);
			checking->refresh_disable_status();
			new_disable = checking->is_status(STATUS_DISABLED);
			if (pre_disable != new_disable && checking->is_status(STATUS_EFFECT_ENABLED)) {
				checking->filter_disable_related_cards();
				if (pre_disable)
					checking->set_status(STATUS_TO_ENABLE, TRUE);
				else
					checking->set_status(STATUS_TO_DISABLE, TRUE);
			}
		}
		for (auto& pcard : checked) {
			if(pcard->is_status(STATUS_DISABLED) && pcard->is_status(STATUS_TO_DISABLE) && !pcard->is_status(STATUS_TO_ENABLE))
				pcard->reset(RESET_DISABLE, RESET_EVENT);
			pcard->set_status(STATUS_TO_ENABLE + STATUS_TO_DISABLE, FALSE);
		}
	} while(effects.disable_check_list.size());
}
// adjust check_unique_onfield(), EFFECT_SELF_DESTROY, EFFECT_SELF_TOGRAVE
void field::adjust_self_destroy_set() {
	if(core.selfdes_disabled || !core.self_destroy_set.empty() || !core.self_tograve_set.empty())
		return;
	card_set cset;
	for(uint8 p = 0; p < 2; ++p) {
		for(uint8 i = 0; i < 5; ++i) {
			card* pcard = player[p].list_mzone[i];
			if(pcard && pcard->is_position(POS_FACEUP))
				cset.insert(pcard);
		}
		for(uint8 i = 0; i < 8; ++i) {
			card* pcard = player[p].list_szone[i];
			if(pcard && pcard->is_position(POS_FACEUP))
				cset.insert(pcard);
		}
	}
	core.self_destroy_set.clear();
	core.self_tograve_set.clear();
	effect* peffect;
	for(auto cit = cset.begin(); cit != cset.end(); ++cit) {
		card* pcard = *cit;
		if((!pcard->is_status(STATUS_DISABLED) && (peffect = check_unique_onfield(pcard, pcard->current.controler)))
		        || (peffect = pcard->is_affected_by_effect(EFFECT_SELF_DESTROY))) {
			core.self_destroy_set.insert(pcard);
			pcard->current.reason_effect = peffect;
			pcard->current.reason_player = peffect->get_handler_player();
		}
	}
	if(core.global_flag & GLOBALFLAG_SELF_TOGRAVE) {
		for(auto cit = cset.begin(); cit != cset.end(); ++cit) {
			card* pcard = *cit;
			if(peffect = pcard->is_affected_by_effect(EFFECT_SELF_TOGRAVE)) {
				core.self_tograve_set.insert(pcard);
				pcard->current.reason_effect = peffect;
				pcard->current.reason_player = peffect->get_handler_player();
			}
		}
	}
	if(!core.self_destroy_set.empty() || !core.self_tograve_set.empty())
		add_process(PROCESSOR_SELF_DESTROY, 0, 0, 0, 0, 0);
}
void field::add_unique_card(card* pcard) {
	uint8 con = pcard->current.controler;
	if(pcard->unique_pos[0])
		core.unique_cards[con].insert(pcard);
	if(pcard->unique_pos[1])
		core.unique_cards[1 - con].insert(pcard);
	pcard->unique_uid = infos.copy_id++;
}

void field::remove_unique_card(card* pcard) {
	uint8 con = pcard->current.controler;
	if(con == PLAYER_NONE)
		return;
	if(pcard->unique_pos[0])
		core.unique_cards[con].erase(pcard);
	if(pcard->unique_pos[1])
		core.unique_cards[1 - con].erase(pcard);
}

effect* field::check_unique_onfield(card* pcard, uint8 controler) {
	if(!pcard->unique_code)
		return 0;
	for(auto& ucard : core.unique_cards[controler]) {
		if((ucard != pcard) && ucard->get_status(STATUS_EFFECT_ENABLED) && (ucard->unique_code == pcard->unique_code)
			&& (!(pcard->current.location & LOCATION_ONFIELD) || pcard->is_position(POS_FACEDOWN) || (ucard->unique_uid < pcard->unique_uid)))
			return pcard->unique_effect;
	}
	return 0;
}

int32 field::check_spsummon_once(card* pcard, uint8 playerid) {
	if(pcard->spsummon_code == 0)
		return TRUE;
	auto iter = core.spsummon_once_map[playerid].find(pcard->spsummon_code);
	return (iter == core.spsummon_once_map[playerid].end()) || (iter->second == 0);
}

void field::check_card_counter(card* pcard, int32 counter_type, int32 playerid) {
	auto& counter_map = (counter_type == 1) ? core.summon_counter :
						(counter_type == 2) ? core.normalsummon_counter :
						(counter_type == 3) ? core.spsummon_counter :
						(counter_type == 4) ? core.flipsummon_counter : core.attack_counter;
	for(auto iter = counter_map.begin(); iter != counter_map.end(); ++iter) {
		auto& info = iter->second;
		if((playerid == 0) && (info.second & 0xffff) != 0)
			continue;
		if((playerid == 1) && (info.second & 0xffff0000) != 0)
			continue;
		if(info.first) {
			pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
			if(!pduel->lua->check_condition(info.first, 1)) {
				if(playerid == 0)
					info.second += 0x1;
				else
					info.second += 0x10000;
			}
		}
	}
}
void field::check_chain_counter(effect* peffect, int32 playerid, int32 chainid, bool cancel) {
	for(auto iter = core.chain_counter.begin(); iter != core.chain_counter.end(); ++iter) {
		auto& info = iter->second;
		if(info.first) {
			pduel->lua->add_param(peffect, PARAM_TYPE_EFFECT);
			pduel->lua->add_param(playerid, PARAM_TYPE_INT);
			pduel->lua->add_param(chainid, PARAM_TYPE_INT);
			if(!pduel->lua->check_condition(info.first, 3)) {
				if(playerid == 0) {
					if(!cancel)
						info.second += 0x1;
					else if(info.second & 0xffff)
						info.second -= 0x1;
				} else {
					if(!cancel)
						info.second += 0x10000;
					else if(info.second & 0xffff0000)
						info.second -= 0x10000;
				}
			}
		}
	}
}
void field::set_spsummon_counter(uint8 playerid, bool add, bool chain) {
	if(add) {
		core.spsummon_state_count[playerid]++;
		if(chain)
			core.spsummon_state_count_rst[playerid]++;
	} else {
		if(chain) {
			core.spsummon_state_count[playerid] -= core.spsummon_state_count_rst[playerid];
			core.spsummon_state_count_rst[playerid] = 0;
		} else
			core.spsummon_state_count[playerid]--;
	}
	if(core.global_flag & GLOBALFLAG_SPSUMMON_COUNT) {
		for(auto iter = effects.spsummon_count_eff.begin(); iter != effects.spsummon_count_eff.end(); ++iter) {
			effect* peffect = *iter;
			card* pcard = peffect->handler;
			if(add) {
				if(pcard->is_status(STATUS_EFFECT_ENABLED) && !pcard->is_status(STATUS_DISABLED) && pcard->is_position(POS_FACEUP)) {
					if(((playerid == pcard->current.controler) && peffect->s_range) || ((playerid != pcard->current.controler) && peffect->o_range)) {
						pcard->spsummon_counter[playerid]++;
						if(chain)
							pcard->spsummon_counter_rst[playerid]++;
					}
				}
			} else {
				pcard->spsummon_counter[playerid] -= pcard->spsummon_counter_rst[playerid];
				pcard->spsummon_counter_rst[playerid] = 0;
			}
		}
	}
}
int32 field::check_spsummon_counter(uint8 playerid, uint8 ct) {
	if(core.global_flag & GLOBALFLAG_SPSUMMON_COUNT) {
		for(auto iter = effects.spsummon_count_eff.begin(); iter != effects.spsummon_count_eff.end(); ++iter) {
			effect* peffect = *iter;
			card* pcard = peffect->handler;
			uint16 val = (uint16)peffect->value;
			if(pcard->is_status(STATUS_EFFECT_ENABLED) && !pcard->is_status(STATUS_DISABLED) && pcard->is_position(POS_FACEUP)) {
				if(pcard->spsummon_counter[playerid] + ct > val)
					return FALSE;
			}
		}
	}
	return TRUE;
}
int32 field::check_lp_cost(uint8 playerid, uint32 lp) {
	effect_set eset;
	int32 val = lp;
	if(lp == 0)
		return TRUE;
	filter_player_effect(playerid, EFFECT_LPCOST_CHANGE, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		pduel->lua->add_param(core.reason_effect, PARAM_TYPE_EFFECT);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		pduel->lua->add_param(val, PARAM_TYPE_INT);
		val = eset[i]->get_value(3);
		if(val <= 0)
			return TRUE;
	}
	tevent e;
	e.event_cards = 0;
	e.event_player = playerid;
	e.event_value = lp;
	e.reason = 0;
	e.reason_effect = core.reason_effect;
	e.reason_player = playerid;
	if(effect_replace_check(EFFECT_LPCOST_REPLACE, e))
		return true;
	cost[playerid].amount += val;
	if(cost[playerid].amount <= player[playerid].lp)
		return TRUE;
	return FALSE;
}
void field::save_lp_cost() {
	for(uint8 playerid = 0; playerid < 2; ++playerid) {
		if(cost[playerid].count < 8)
			cost[playerid].lpstack[cost[playerid].count] = cost[playerid].amount;
		cost[playerid].count++;
	}
}
void field::restore_lp_cost() {
	for(uint8 playerid = 0; playerid < 2; ++playerid) {
		cost[playerid].count--;
		if(cost[playerid].count < 8)
			cost[playerid].amount = cost[playerid].lpstack[cost[playerid].count];
	}
}
uint32 field::get_field_counter(uint8 self, uint8 s, uint8 o, uint16 countertype) {
	uint8 c = s;
	uint32 count = 0;
	for(int p = 0; p < 2; ++p) {
		if(c) {
			for(auto& pcard : player[self].list_mzone) {
				if(pcard)
					count += pcard->get_counter(countertype);
			}
			for(int i = 0; i < 8; ++i) {
				if(player[self].list_szone[i])
					count += player[self].list_szone[i]->get_counter(countertype);
			}
		}
		self = 1 - self;
		c = o;
	}
	return count;
}
int32 field::effect_replace_check(uint32 code, const tevent& e) {
	auto pr = effects.continuous_effect.equal_range(code);
	for (; pr.first != pr.second; ++pr.first) {
		effect* peffect = pr.first->second;
		if(peffect->is_activateable(peffect->get_handler_player(), e))
			return TRUE;
	}
	return FALSE;
}
int32 field::get_attack_target(card* pcard, card_vector* v, uint8 chain_attack) {
	uint8 p = pcard->current.controler;
	effect* peffect;
	pcard->operation_param = 0;
	card_vector must_be_attack;
	card_vector* pv;
	card_vector::iterator cit;
	for(uint32 i = 0; i < 5; ++i) {
		auto atarget = player[1 - p].list_mzone[i];
		if(atarget && atarget->is_affected_by_effect(EFFECT_MUST_BE_ATTACKED, pcard))
			must_be_attack.push_back(atarget);
	}
	if(pcard->attack_all_target && (peffect = pcard->is_affected_by_effect(EFFECT_ATTACK_ALL))) {
		if(pcard->announced_cards.size()) {
			if(must_be_attack.size())
				pv = &must_be_attack;
			else
				pv = &player[1 - p].list_mzone;
			for(auto& atarget : *pv) {
				if(!atarget)
					continue;
				auto it = pcard->announced_cards.find(atarget->fieldid_r);
				if(it != pcard->announced_cards.end()) {
					if(it->second.second >= peffect->get_value(atarget))
						continue;
				}
				if(atarget->is_affected_by_effect(EFFECT_IGNORE_BATTLE_TARGET))
					continue;
				if(atarget->is_affected_by_effect(EFFECT_CANNOT_BE_BATTLE_TARGET, pcard))
					continue;
				if(pcard->is_affected_by_effect(EFFECT_CANNOT_SELECT_BATTLE_TARGET, atarget))
					continue;
				pduel->lua->add_param(atarget, PARAM_TYPE_CARD);
				if(!peffect->check_value_condition(1))
					continue;
				v->push_back(atarget);
			}
			return must_be_attack.size() ? TRUE : FALSE;
		}
	} else if(!chain_attack) {
		uint32 extrac = 0;
		if((peffect = pcard->is_affected_by_effect(EFFECT_EXTRA_ATTACK)))
			extrac = peffect->get_value(pcard);
		if(pcard->announce_count >= extrac + 1)
			return FALSE;
	}
	uint32 mcount = 0;
	if(must_be_attack.size())
		pv = &must_be_attack;
	else
		pv = &player[1 - p].list_mzone;
	for(auto& atarget : *pv) {
		if(!atarget)
			continue;
		if(atarget->is_affected_by_effect(EFFECT_IGNORE_BATTLE_TARGET))
			continue;
		mcount++;
		if(atarget->is_affected_by_effect(EFFECT_CANNOT_BE_BATTLE_TARGET, pcard))
			continue;
		if(pcard->is_affected_by_effect(EFFECT_CANNOT_SELECT_BATTLE_TARGET, atarget))
			continue;
		if(chain_attack && core.chain_attack_target && atarget != core.chain_attack_target)
			continue;
		v->push_back(atarget);
	}
	if(must_be_attack.size())
		return TRUE;
	if((mcount == 0 || pcard->is_affected_by_effect(EFFECT_DIRECT_ATTACK)) 
			&& !pcard->is_affected_by_effect(EFFECT_CANNOT_DIRECT_ATTACK) 
			&& !(chain_attack && core.chain_attack_target))
		pcard->operation_param = 1;
	return must_be_attack.size() ? TRUE : FALSE;
}
void field::attack_all_target_check() {
	if(!core.attacker)
		return;
	if(!core.attack_target) {
		core.attacker->attack_all_target = FALSE;
		return;
	}
	effect* peffect = core.attacker->is_affected_by_effect(EFFECT_ATTACK_ALL);
	if(!peffect)
		return;
	if(!peffect->get_value(core.attack_target))
		core.attacker->attack_all_target = FALSE;
}
int32 field::check_synchro_material(card* pcard, int32 findex1, int32 findex2, int32 min, int32 max, card* smat, group* mg) {
	card* tuner;
	if(core.global_flag & GLOBALFLAG_MUST_BE_SMATERIAL) {
		effect_set eset;
		filter_player_effect(pcard->current.controler, EFFECT_MUST_BE_SMATERIAL, &eset);
		if(eset.size())
			return check_tuner_material(pcard, eset[0]->handler, findex1, findex2, min, max, smat, mg);
	}
	if(mg) {
		for(auto cit = mg->container.begin(); cit != mg->container.end(); ++cit) {
			tuner = *cit;
			if(check_tuner_material(pcard, tuner, findex1, findex2, min, max, smat, mg))
				return TRUE;
		}
	} else {
		for(uint8 p = 0; p < 2; ++p) {
			for(int32 i = 0; i < 5; ++i) {
				tuner = player[p].list_mzone[i];
				if(check_tuner_material(pcard, tuner, findex1, findex2, min, max, smat, mg))
					return TRUE;
			}
		}
	}
	return FALSE;
}
int32 field::check_tuner_material(card* pcard, card* tuner, int32 findex1, int32 findex2, int32 min, int32 max, card* smat, group* mg) {
	if(tuner && tuner->is_position(POS_FACEUP) && (tuner->get_type() & TYPE_TUNER) && tuner->is_can_be_synchro_material(pcard)) {
		effect* pcheck = tuner->is_affected_by_effect(EFFECT_SYNCHRO_CHECK);
		if(pcheck)
			pcheck->get_value(tuner);
		if((mg && !mg->has_card(tuner)) || !pduel->lua->check_matching(tuner, findex1, 0)) {
			pduel->restore_assumes();
			return FALSE;
		}
		effect* pcustom = tuner->is_affected_by_effect(EFFECT_SYNCHRO_MATERIAL_CUSTOM, pcard);
		if(pcustom) {
			if(!pcustom->target) {
				pduel->restore_assumes();
				return FALSE;
			}
			pduel->lua->add_param(pcustom, PARAM_TYPE_EFFECT);
			pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
			pduel->lua->add_param(findex2, PARAM_TYPE_INDEX);
			pduel->lua->add_param(min, PARAM_TYPE_INT);
			pduel->lua->add_param(max, PARAM_TYPE_INT);
			if(pduel->lua->check_condition(pcustom->target, 5)) {
				pduel->restore_assumes();
				return TRUE;
			}
		} else {
			int32 lv = pcard->get_level();
			card_vector nsyn;
			int32 mcount = 1;
			nsyn.push_back(tuner);
			tuner->operation_param = tuner->get_synchro_level(pcard);
			if(smat) {
				if(pcheck)
					pcheck->get_value(smat);
				if(!smat->is_position(POS_FACEUP) || !smat->is_can_be_synchro_material(pcard, tuner) || !pduel->lua->check_matching(smat, findex2, 0)) {
					pduel->restore_assumes();
					return FALSE;
				}
				min--;
				max--;
				nsyn.push_back(smat);
				smat->operation_param = smat->get_synchro_level(pcard);
				mcount++;
				if(min == 0) {
					if(check_with_sum_limit_m(nsyn, lv, 0, 0, 0, 2)) {
						pduel->restore_assumes();
						return TRUE;
					}
					if(max == 0) {
						pduel->restore_assumes();
						return FALSE;
					}
				}
			}
			if(mg) {
				for(auto cit = mg->container.begin(); cit != mg->container.end(); ++cit) {
					card* pm = *cit;
					if(pm != tuner && pm != smat && pm->is_can_be_synchro_material(pcard, tuner)) {
						if(pcheck)
							pcheck->get_value(pm);
						if(pm->current.location == LOCATION_MZONE && !pm->is_position(POS_FACEUP))
							continue;
						if(!pduel->lua->check_matching(pm, findex2, 0))
							continue;
						nsyn.push_back(pm);
						pm->operation_param = pm->get_synchro_level(pcard);
					}
				}
			} else {
				for(uint8 p = 0; p < 2; ++p) {
					for(int32 i = 0; i < 5; ++i) {
						card* pm = player[p].list_mzone[i];
						if(pm && pm != tuner && pm != smat && pm->is_position(POS_FACEUP) && pm->is_can_be_synchro_material(pcard, tuner)) {
							if(pcheck)
								pcheck->get_value(pm);
							if(!pduel->lua->check_matching(pm, findex2, 0))
								continue;
							nsyn.push_back(pm);
							pm->operation_param = pm->get_synchro_level(pcard);
						}
					}
				}
			}
			if(!(core.global_flag & GLOBALFLAG_SCRAP_CHIMERA)) {
				if(check_with_sum_limit_m(nsyn, lv, 0, min, max, mcount)) {
					pduel->restore_assumes();
					return TRUE;
				}
			} else {
				effect* pscrap = 0;
				for(auto cit = nsyn.begin(); cit != nsyn.end(); ++cit) {
					pscrap = (*cit)->is_affected_by_effect(EFFECT_SCRAP_CHIMERA);
					if(pscrap)
						break;
				}
				if(pscrap) {
					card_vector nsyn_filtered;
					for(auto cit = nsyn.begin(); cit != nsyn.end(); ++cit) {
						if(!pscrap->get_value(*cit))
							nsyn_filtered.push_back(*cit);
					}
					if(nsyn_filtered.size() == nsyn.size()) {
						if(check_with_sum_limit_m(nsyn, lv, 0, min, max, mcount)) {
							pduel->restore_assumes();
							return TRUE;
						}
					} else {
						bool mfiltered = true;
						for(int32 i = 0; i < mcount; ++i) {
							if(pscrap->get_value(nsyn[i]))
								mfiltered = false;
						}
						if(mfiltered && check_with_sum_limit_m(nsyn_filtered, lv, 0, min, max, mcount)) {
							pduel->restore_assumes();
							return TRUE;
						}
						for(int32 i = 0; i < mcount; ++i) {
							if(nsyn[i]->is_affected_by_effect(EFFECT_SCRAP_CHIMERA)) {
								pduel->restore_assumes();
								return FALSE;
							}
						}
						card_vector nsyn_removed;
						for(auto cit = nsyn.begin(); cit != nsyn.end(); ++cit) {
							if(!(*cit)->is_affected_by_effect(EFFECT_SCRAP_CHIMERA))
								nsyn_removed.push_back(*cit);
						}
						if(check_with_sum_limit_m(nsyn_removed, lv, 0, min, max, mcount)) {
							pduel->restore_assumes();
							return TRUE;
						}
					}
				} else {
					if(check_with_sum_limit_m(nsyn, lv, 0, min, max, mcount)) {
						pduel->restore_assumes();
						return TRUE;
					}
				}
			}
		}
	}
	pduel->restore_assumes();
	return FALSE;
}
int32 field::check_with_sum_limit(const card_vector& mats, int32 acc, int32 index, int32 count, int32 min, int32 max) {
	if(count > max)
		return FALSE;
	while(index < (int32)mats.size()) {
		int32 op1 = mats[index]->operation_param & 0xffff;
		int32 op2 = (mats[index]->operation_param >> 16) & 0xffff;
		if((op1 == acc || op2 == acc) && count >= min)
			return TRUE;
		index++;
		if(acc > op1 && check_with_sum_limit(mats, acc - op1, index, count + 1, min, max))
			return TRUE;
		if(op2 && acc > op2 && check_with_sum_limit(mats, acc - op2, index, count + 1, min, max))
			return TRUE;
	}
	return FALSE;
}
int32 field::check_with_sum_limit_m(const card_vector& mats, int32 acc, int32 index, int32 min, int32 max, int32 must_count) {
	if(acc == 0)
		return index == must_count && 0 >= min && 0 <= max;
	if(index == must_count)
		return check_with_sum_limit(mats, acc, index, 1, min, max);
	if(index >= (int32)mats.size())
		return FALSE;
	int32 op1 = mats[index]->operation_param & 0xffff;
	int32 op2 = (mats[index]->operation_param >> 16) & 0xffff;
	if(acc >= op1 && check_with_sum_limit_m(mats, acc - op1, index + 1, min, max, must_count))
		return TRUE;
	if(op2 && acc >= op2 && check_with_sum_limit_m(mats, acc - op2, index + 1, min, max, must_count))
		return TRUE;
	return FALSE;
}
int32 field::check_xyz_material(card* scard, int32 findex, int32 lv, int32 min, int32 max, group* mg) {
	get_xyz_material(scard, findex, lv, max, mg);
	return (int32)core.xmaterial_lst.size() >= min;
}
int32 field::is_player_can_draw(uint8 playerid) {
	return !is_player_affected_by_effect(playerid, EFFECT_CANNOT_DRAW);
}
int32 field::is_player_can_discard_deck(uint8 playerid, int32 count) {
	if(player[playerid].list_main.size() < (uint32)count)
		return FALSE;
	return !is_player_affected_by_effect(playerid, EFFECT_CANNOT_DISCARD_DECK);
}
int32 field::is_player_can_discard_deck_as_cost(uint8 playerid, int32 count) {
	if(player[playerid].list_main.size() < (uint32)count)
		return FALSE;
	if(is_player_affected_by_effect(playerid, EFFECT_CANNOT_DISCARD_DECK))
		return FALSE;
	if((count == 1) && core.deck_reversed)
		return player[playerid].list_main.back()->is_capable_cost_to_grave(playerid);
	effect_set eset;
	filter_field_effect(EFFECT_TO_GRAVE_REDIRECT, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		uint32 redirect = eset[i]->get_value();
		if((redirect & LOCATION_REMOVED) && player[playerid].list_main.back()->is_affected_by_effect(EFFECT_CANNOT_REMOVE))
			continue;
		uint8 p = eset[i]->get_handler_player();
		if((p == playerid && eset[i]->s_range & LOCATION_DECK) || (p != playerid && eset[i]->o_range & LOCATION_DECK))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_discard_hand(uint8 playerid, card * pcard, effect * peffect, uint32 reason) {
	if(pcard->current.location != LOCATION_HAND)
		return FALSE;
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_DISCARD_HAND, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(peffect, PARAM_TYPE_EFFECT);
		pduel->lua->add_param(reason, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 4))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_summon(uint8 playerid) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_SUMMON, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_summon(uint32 sumtype, uint8 playerid, card * pcard) {
	effect_set eset;
	sumtype |= SUMMON_TYPE_NORMAL;
	filter_player_effect(playerid, EFFECT_CANNOT_SUMMON, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		pduel->lua->add_param(sumtype, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 4))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_mset(uint32 sumtype, uint8 playerid, card * pcard) {
	effect_set eset;
	sumtype |= SUMMON_TYPE_NORMAL;
	filter_player_effect(playerid, EFFECT_CANNOT_MSET, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		pduel->lua->add_param(sumtype, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 4))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_sset(uint8 playerid, card * pcard) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_SSET, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 3))
			return FALSE;
	}
	return TRUE;
}
// check player-effect EFFECT_CANNOT_SPECIAL_SUMMON without target
int32 field::is_player_can_spsummon(uint8 playerid) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_SPECIAL_SUMMON, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
	}
	return is_player_can_spsummon_count(playerid, 1);
}
int32 field::is_player_can_spsummon(effect * peffect, uint32 sumtype, uint8 sumpos, uint8 playerid, uint8 toplayer, card * pcard) {
	effect_set eset;
	if(pcard->is_affected_by_effect(EFFECT_CANNOT_SPECIAL_SUMMON))
		return FALSE;
	if(pcard->is_affected_by_effect(EFFECT_FORBIDDEN))
		return FALSE;
	sumtype |= SUMMON_TYPE_SPECIAL;
	if(sumpos & POS_FACEDOWN && is_player_affected_by_effect(playerid, EFFECT_DEVINE_LIGHT))
		sumpos = (sumpos & POS_FACEUP) | (sumpos >> 1);
	filter_player_effect(playerid, EFFECT_CANNOT_SPECIAL_SUMMON, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		pduel->lua->add_param(sumtype, PARAM_TYPE_INT);
		pduel->lua->add_param(sumpos, PARAM_TYPE_INT);
		pduel->lua->add_param(toplayer, PARAM_TYPE_INT);
		pduel->lua->add_param(peffect, PARAM_TYPE_EFFECT);
		if (pduel->lua->check_condition(eset[i]->target, 7))
			return FALSE;
	}
	if(!check_spsummon_once(pcard, playerid))
		return FALSE;
	if(!check_spsummon_counter(playerid))
		return FALSE;
	return TRUE;
}
int32 field::is_player_can_flipsummon(uint8 playerid, card * pcard) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_FLIP_SUMMON, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 3))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_spsummon_monster(uint8 playerid, uint8 toplayer, uint8 sumpos, card_data * pdata) {
	temp_card->data = *pdata;
	return is_player_can_spsummon(core.reason_effect, SUMMON_TYPE_SPECIAL, sumpos, playerid, toplayer, temp_card);
}
int32 field::is_player_can_release(uint8 playerid, card * pcard) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_RELEASE, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 3))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_spsummon_count(uint8 playerid, uint32 count) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_LEFT_SPSUMMON_COUNT, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		pduel->lua->add_param(core.reason_effect, PARAM_TYPE_EFFECT);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		int32 v = eset[i]->get_value(2);
		if(v < (int32)count)
			return FALSE;
	}
	return check_spsummon_counter(playerid, count);
}
int32 field::is_player_can_place_counter(uint8 playerid, card * pcard, uint16 countertype, uint16 count) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_PLACE_COUNTER, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 3))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_remove_counter(uint8 playerid, card * pcard, uint8 s, uint8 o, uint16 countertype, uint16 count, uint32 reason) {
	if((pcard && pcard->get_counter(countertype) >= count) || (!pcard && get_field_counter(playerid, s, o, countertype) >= count))
		return TRUE;
	auto pr = effects.continuous_effect.equal_range(EFFECT_RCOUNTER_REPLACE + countertype);
	effect* peffect;
	tevent e;
	e.event_cards = 0;
	e.event_player = playerid;
	e.event_value = count;
	e.reason = reason;
	e.reason_effect = core.reason_effect;
	e.reason_player = playerid;
	for (; pr.first != pr.second; ++pr.first) {
		peffect = pr.first->second;
		if(peffect->is_activateable(peffect->get_handler_player(), e))
			return TRUE;
	}
	return FALSE;
}
int32 field::is_player_can_remove_overlay_card(uint8 playerid, card * pcard, uint8 s, uint8 o, uint16 min, uint32 reason) {
	if((pcard && pcard->xyz_materials.size() >= min) || (!pcard && get_overlay_count(playerid, s, o) >= min))
		return TRUE;
	auto pr = effects.continuous_effect.equal_range(EFFECT_OVERLAY_REMOVE_REPLACE);
	effect* peffect;
	tevent e;
	e.event_cards = 0;
	e.event_player = playerid;
	e.event_value = min;
	e.reason = reason;
	e.reason_effect = core.reason_effect;
	e.reason_player = playerid;
	for (; pr.first != pr.second; ++pr.first) {
		peffect = pr.first->second;
		if(peffect->is_activateable(peffect->get_handler_player(), e))
			return TRUE;
	}
	return FALSE;
}
int32 field::is_player_can_send_to_grave(uint8 playerid, card * pcard) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_TO_GRAVE, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 3))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_send_to_hand(uint8 playerid, card * pcard) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_TO_HAND, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 3))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_send_to_deck(uint8 playerid, card * pcard) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_TO_DECK, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 3))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_player_can_remove(uint8 playerid, card * pcard) {
	effect_set eset;
	filter_player_effect(playerid, EFFECT_CANNOT_REMOVE, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		if(!eset[i]->target)
			return FALSE;
		pduel->lua->add_param(eset[i], PARAM_TYPE_EFFECT);
		pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
		pduel->lua->add_param(playerid, PARAM_TYPE_INT);
		if (pduel->lua->check_condition(eset[i]->target, 3))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_chain_negatable(uint8 chaincount, uint8 naga_check) {
	effect_set eset;
	if(chaincount < 0 || chaincount > core.current_chain.size())
		return FALSE;
	effect* peffect;
	if(chaincount == 0)
		peffect = core.current_chain.back().triggering_effect;
	else
		peffect = core.current_chain[chaincount - 1].triggering_effect;
	if(naga_check && peffect->is_flag(EFFECT_FLAG2_NAGA))
		return FALSE;
	if(peffect->is_flag(EFFECT_FLAG_CANNOT_DISABLE))
		return FALSE;
	filter_field_effect(EFFECT_CANNOT_INACTIVATE, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		pduel->lua->add_param(chaincount, PARAM_TYPE_INT);
		if(eset[i]->check_value_condition(1))
			return FALSE;
	}
	return TRUE;
}
int32 field::is_chain_disablable(uint8 chaincount, uint8 naga_check) {
	effect_set eset;
	if(chaincount < 0 || chaincount > core.current_chain.size())
		return FALSE;
	effect* peffect;
	if(chaincount == 0)
		peffect = core.current_chain.back().triggering_effect;
	else
		peffect = core.current_chain[chaincount - 1].triggering_effect;
	if(naga_check && peffect->is_flag(EFFECT_FLAG2_NAGA))
		return FALSE;
	if(peffect->is_flag(EFFECT_FLAG_CANNOT_DISABLE))
		return FALSE;
	filter_field_effect(EFFECT_CANNOT_DISEFFECT, &eset);
	for(int32 i = 0; i < eset.size(); ++i) {
		pduel->lua->add_param(chaincount, PARAM_TYPE_INT);
		if(eset[i]->check_value_condition(1))
			return FALSE;
	}
	return TRUE;
}
int32 field::check_chain_target(uint8 chaincount, card * pcard) {
	if(chaincount < 0 || chaincount > core.current_chain.size())
		return FALSE;
	chain* pchain;
	if(chaincount == 0)
		pchain = &core.current_chain.back();
	else
		pchain = &core.current_chain[chaincount - 1];
	effect* peffect = pchain->triggering_effect;
	uint8 tp = pchain->triggering_player;
	if(!(peffect->is_flag(EFFECT_FLAG_CARD_TARGET)) || !peffect->target)
		return FALSE;
	if(!pcard->is_capable_be_effect_target(peffect, tp))
		return false;
	pduel->lua->add_param(peffect, PARAM_TYPE_EFFECT);
	pduel->lua->add_param(tp, PARAM_TYPE_INT);
	pduel->lua->add_param(pchain->evt.event_cards , PARAM_TYPE_GROUP);
	pduel->lua->add_param(pchain->evt.event_player, PARAM_TYPE_INT);
	pduel->lua->add_param(pchain->evt.event_value, PARAM_TYPE_INT);
	pduel->lua->add_param(pchain->evt.reason_effect , PARAM_TYPE_EFFECT);
	pduel->lua->add_param(pchain->evt.reason, PARAM_TYPE_INT);
	pduel->lua->add_param(pchain->evt.reason_player, PARAM_TYPE_INT);
	pduel->lua->add_param((ptr)0, PARAM_TYPE_INT);
	pduel->lua->add_param(pcard, PARAM_TYPE_CARD);
	return pduel->lua->check_condition(peffect->target, 10);
}
int32 field::is_able_to_enter_bp() {
	return ((core.duel_options & DUEL_ATTACK_FIRST_TURN) || infos.turn_id != 1)
	        && infos.phase < PHASE_BATTLE_START
	        && !is_player_affected_by_effect(infos.turn_player, EFFECT_CANNOT_BP);
}
