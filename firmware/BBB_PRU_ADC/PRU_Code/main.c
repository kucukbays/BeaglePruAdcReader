/*
 * Source Modified by Furkan Küçükbay
 * Based on the examples distributed by TI and improved by Zubeen Tolani.
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *
 *	* Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the
 *	  distribution.
 *
 *	* Neither the name of Texas Instruments Incorporated nor the names of
 *	  its contributors may be used to endorse or promote products derived
 *	  from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdio.h>
#include <pru_cfg.h>
#include <pru_intc.h>
#include <rsc_types.h>
#include <pru_virtqueue.h>
#include <pru_rpmsg.h>
#include "resource_table_1.h"
#include <time.h>

volatile register uint32_t __R30;
volatile register uint32_t __R31;

/* Host-1 Interrupt sets bit 31 in register R31 */
#define HOST_INT				((uint32_t) 1 << 31)	

/* The PRU-ICSS system events used for RPMsg are defined in the Linux device tree
 * PRU0 uses system event 16 (To ARM) and 17 (From ARM)
 * PRU1 uses system event 18 (To ARM) and 19 (From ARM)
 */
#define TO_ARM_HOST				18	
#define FROM_ARM_HOST			19

/*
 * Using the name 'rpmsg-pru' will probe the rpmsg_pru driver found
 * at linux-x.y.z/drivers/rpmsg/rpmsg_pru.c
 */
#define CHAN_NAME					"rpmsg-pru"
#define CHAN_DESC					"Channel 31"
#define CHAN_PORT					31




/*
* For sending messages from PRU to ARM, rpmsg_pru30 channel should be opened.
*/
#define CHAN_DESC_2 "Channel 30"
#define CHAN_PORT_2 30




/*
 * Used to make sure the Linux drivers are ready for RPMsg communication
 * Found at linux-x.y.z/include/uapi/linux/virtio_config.h
 */
#define VIRTIO_CONFIG_S_DRIVER_OK	4


 /*
 * Used to check the state of 0 bit of the r31 ie
 * the state of pr1_pru1_pru_r31_0. This gpio can be 
 * muxed to P8_45.
 */
#define CHECK_BIT	0x0004


int32_t payload[10];

/*
 * main.c
 */
void main(void)
{
	struct pru_rpmsg_transport transport;
	uint16_t src, dst, len;
	uint32_t data = 0x00000000;
	int32_t signedData = 0x00000000;
	uint16_t dataCounter = 0;
	uint32_t prev_gpio_state;
	volatile uint8_t *status;
	
	/* allow OCP master port access by the PRU so the PRU can read external memories */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

	/* clear the status of the PRU-ICSS system event that the ARM will use to 'kick' us */
	CT_INTC.SICR_bit.STS_CLR_IDX = FROM_ARM_HOST;

	/* Make sure the Linux drivers are ready for RPMsg communication */
	status = &resourceTable.rpmsg_vdev.status;
	while (!(*status & VIRTIO_CONFIG_S_DRIVER_OK));

	/* Initialize pru_virtqueue corresponding to vring0 (PRU to ARM Host direction) */
	pru_virtqueue_init(&transport.virtqueue0, &resourceTable.rpmsg_vring0, TO_ARM_HOST, FROM_ARM_HOST);

	/* Initialize pru_virtqueue corresponding to vring1 (ARM Host to PRU direction) */
	pru_virtqueue_init(&transport.virtqueue1, &resourceTable.rpmsg_vring1, TO_ARM_HOST, FROM_ARM_HOST);

	/* Create the RPMsg channel between the PRU and ARM user space using the transport structure. */
	while (pru_rpmsg_channel(RPMSG_NS_CREATE, &transport, CHAN_NAME, CHAN_DESC, CHAN_PORT) != PRU_RPMSG_SUCCESS);



	/* Create the rpmsg_pru30 channel */
	while (pru_rpmsg_channel(RPMSG_NS_CREATE, &transport, CHAN_NAME, CHAN_DESC_2, CHAN_PORT_2) != PRU_RPMSG_SUCCESS);


	int i;
	while (1) {
		/* Check bit 30 of register R31 to see if the ARM has kicked us */
		__R30 = 0x00000000;
		if (__R31 & HOST_INT) {
			/* Clear the event status */
			CT_INTC.SICR_bit.STS_CLR_IDX = FROM_ARM_HOST;
			/* Receive all available messages, multiple messages can be sent per kick */
			while (pru_rpmsg_receive(&transport, &src, &dst, payload, &len) == PRU_RPMSG_SUCCESS) {	
				while(1){

					if ((__R31 ^ prev_gpio_state) & CHECK_BIT) {
						prev_gpio_state = __R31 & CHECK_BIT;

						__R30 = __R30 & ( (uint32_t)0 << 1); // SCLK made LOW before shift&read.
		
						// If pruin changes to LOW, Data Reading starts.
						if( !(__R31 & ( (uint32_t)1 << 2)) ){
							
							for( i = 0; i < 24; i = i + 1){
								__R30 = __R30 | ( (uint32_t)1 << 1);  // SCLK HIGH.
								__delay_cycles(2000);

								data = data << 1;

								if( __R31 & ( (uint32_t)1 << 2) ){
									// If what comes from pruin is HIGH, shift and LSB is 1.
									data = data | 0x00000001;	
								}
								else{
									data = data & 0xFFFFFFFE;
								}
					
								__R30 = __R30 & ( (uint32_t)0 << 1); //SCLK LOW
								__delay_cycles(1000);	
							}
							
							// --- After getting 24-bit sample, sign extend ---
							// Logical Shift 24-bit data to the left to make MSB of
							// 24-bit appear on 31st bit.
							// Arithmetic Shift 32-bit data by 8 to get sign extend	
							signedData = data;
							signedData = (signedData << 8) >> 8;
							 

							payload[dataCounter] = signedData;
							dataCounter = dataCounter + 1;
							data = 0x00000000;
							signedData = 0x00000000;  

							if( dataCounter == 10){
								dataCounter = 0;
								pru_rpmsg_send(&transport, dst, src, payload, 40);
							}
							
							__R30 = __R30 | (1 << 1); // pruout HIGH
							__delay_cycles(1000);
							__R30 = __R30 & (0 << 1); // pruout LOW
						}	
					}	
				}	
			}
		}
	}
}


