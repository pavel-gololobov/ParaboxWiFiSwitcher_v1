/*
 * protocol.h
 *
 *  Created on: Oct 28, 2016
 *      Author: levap
 */

#ifndef PARABOX_DEVBOARD_PROTOCOL_PROTOCOL_H_
#define PARABOX_DEVBOARD_PROTOCOL_PROTOCOL_H_

#include <stdio.h>
#include <string.h>
#include "action_code.h"
#include "datatype_code.h"
#include "error_code.h"
#include "parameter_code.h"
#include "version_code.h"

struct ParaboxHeader {
	uint8_t		version;
	uint8_t 	action;
	uint16_t 	parameter;
	uint8_t 	index;
	uint8_t		dataType;
	uint8_t		dataLen;
} __attribute__((packed));

#endif /* PARABOX_DEVBOARD_PROTOCOL_PROTOCOL_H_ */
