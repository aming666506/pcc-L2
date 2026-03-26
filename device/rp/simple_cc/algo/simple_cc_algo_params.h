// /*
//  * Simple AIMD CC parameters
//  */

// #ifndef SIMPLE_CC_ALGO_PARAMS_H_
// #define SIMPLE_CC_ALGO_PARAMS_H_

// /* Parameters are fixed-point as expected by DOCA PCC dev helpers */
// /* Rate format: 20-bit fixed point (DOCA_PCC_DEV_LOG_MAX_RATE = 20) */
// /* Mapping: 2^14 = 1Gbps */

// #define SIMPLE_CC_MD_FXP16 (0x8000) /* multiplicative decrease: 0.5 in fxp16 */
// /*#define SIMPLE_CC_AI_FXP20 (1 << 12) additive increase per RTT */
// #define SIMPLE_CC_AI_FXP20 (16384 * 3) /* additive increase per RTT */
// #define SIMPLE_CC_MIN_RATE (1 << 12)  /* init rate: 2^10 = 1K units */

// #define SIMPLE_CC_MD_MAX (1 << 16)
// #define SIMPLE_CC_AI_MAX (1 << 20)  
// #define SIMPLE_CC_RATE_MAX (1 << 15)  /* max rate: 1Gbps (2^14) */



// #endif /* SIMPLE_CC_ALGO_PARAMS_H_ */




/*
 * Simple AIMD CC parameters
 * 修正：解决宏定义/注释矛盾，按DOCA 2^14=1Gbps规则定义，合理设置速率范围
 */



 ///----------------速率低（未修改）
// #ifndef SIMPLE_CC_ALGO_PARAMS_H_
// #define SIMPLE_CC_ALGO_PARAMS_H_

// /* Parameters are fixed-point as expected by DOCA PCC dev helpers */
// /* Rate format: 20-bit fixed point (DOCA_PCC_DEV_LOG_MAX_RATE = 20) */
// /* Core Mapping: 2^14 = 1Gbps (DOCA official rule) */

// // 乘性减因子：fxp16格式，0x8000=0.5（原有值保留，无错）
// #define SIMPLE_CC_MD_FXP16 (0x8000)

// // 加性增因子（无拥塞）：fxp20格式，16384=1Gbps单位/RTT（原有值保留，无错）
// #define SIMPLE_CC_AI_FXP20 (1<<14)

// // 最小速率：1 K units，fxp20格式，1024  ~64Mbps
// #define SIMPLE_CC_MIN_RATE (1 << 10) 

// // 最大速率：fxp20格式，2^14=1Gbps    ~1.56Gbps
// #define SIMPLE_CC_RATE_MAX (1<<14)

// // 各参数的最大值限制（按fxp格式最大值定义，合理范围）
// #define SIMPLE_CC_MD_MAX (1 << 16)    // fxp16格式最大值（65536，对应1.0）
// #define SIMPLE_CC_AI_MAX (1 << 20)    // fxp20格式最大值（1048576，对应1024Gbps）

// #endif /* SIMPLE_CC_ALGO_PARAMS_H_ */

/*

 * Simple AIMD CC parameters

 * 修正：解决宏定义/注释矛盾，按DOCA 2^14=1Gbps规则定义，合理设置速率范围

 */



#ifndef SIMPLE_CC_ALGO_PARAMS_H_

#define SIMPLE_CC_ALGO_PARAMS_H_



/* Parameters are fixed-point as expected by DOCA PCC dev helpers */

/* Rate format: 20-bit fixed point (DOCA_PCC_DEV_LOG_MAX_RATE = 20) */

/* Core Mapping: 2^14 = 1Gbps (DOCA official rule) */



// 乘性减因子：fxp16格式，0x8000=0.5（原有值保留，无错）0xCCCD /* 0.8 in fxp16, 20%减速

#define SIMPLE_CC_MD_FXP16 (0x8000) /* 0x9999 == 0.6 in fxp16, 40%减速 */

// 加性增因子（无拥塞）：fxp20格式，16384=1Gbps单位/RTT（原有值保留，无错）

#define SIMPLE_CC_AI_FXP20 (16384/10)

// 最小速率：fxp20格式，2^14=1Gbps（修正注释+合理值，适配初始化/最小速率限制）

#define SIMPLE_CC_MIN_RATE (1 << 17)

// 最大速率：fxp20格式，2^20=100Gbps

#define SIMPLE_CC_RATE_MAX ((1 << 20))



// 各参数的最大值限制（按fxp格式最大值定义，合理范围）

#define SIMPLE_CC_MD_MAX (1 << 16)    // fxp16格式最大值（65536，对应1.0）

#define SIMPLE_CC_AI_MAX ((1<<14)*100)    // fxp20格式最大值（1048576，对应1024Gbps）

#endif /* SIMPLE_CC_ALGO_PARAMS_H_ */