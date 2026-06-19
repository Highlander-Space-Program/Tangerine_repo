#pragma once

/*
Safety flags - arm/disarm: Igniter system, servos/solenoids, auto sequence
Event groups are a 32 bit variable where each bit can serve as a flag
Can configure for 8 bits but it would negatively affect performance

0b 0000 0000 0000 0000 0000 0000 0000 0000
                                       |||
                                       ||+---- Bit 0: Igniter System Armed
                                       |+----- Bit 1: Auto Ignition Sequence Armed
                                       +------ Bit 2: Servos/Solenoids Armed

Igniter System Armed: Required to fire: e-match (igniter) 

Auto Ignition Sequence Armed: Turns on the auto ignition sequence which utilizes the breakwire
    - once the ignition e-match is fired (by operator), igniter motor begins to burn, melting the breakwire
    - once the breakwire loses continuity the servo run valves is automatically opened by the system

Servos Armed: Required to actuate the servo valves (tangerine operation)

*/
#define SYS_SAFETY_FLAGS_IGNITER_SYSTEM_ARMED_SHIFT 0
#define SYS_SAFETY_FLAGS_AUTO_SEQUENCE_ARMED_SHIFT  1
#define SYS_SAFETY_FLAGS_SERVOS_ARMED_SHIFT         2

#define SAFETY_FLAGS_MASK_IGNITER_SYSTEM_ARMED (1 << SYS_SAFETY_FLAGS_IGNITER_SYSTEM_ARMED_SHIFT) //0x00000001 = 0b0001
#define SAFETY_FLAGS_MASK_AUTO_SEQUENCE_ARMED  (1 << SYS_SAFETY_FLAGS_AUTO_SEQUENCE_ARMED_SHIFT)  //0x00000002 = 0b0010
#define SAFETY_FLAGS_MASK_SERVOS_ARMED         (1 << SYS_SAFETY_FLAGS_SERVOS_ARMED_SHIFT)         //0x00000004 = 0b0100
