// 说明：声码器解码初始化
// 输入参数：
// pDeDataStruct: 编码缓冲变量地址
// Rate: 编码速率选择 (0: 2400 速率, 1: 2200 速率)
// 返回参数：实际的解码运算所需的空间。
short int NVOC_VoDecoder_Init(short int *pDeDataStruct, short int Rate);

// 说明：FEC 解码（20ms 语音帧进行一次解 FEC）
// 输入参数：
// pWordsIn: 解 FEC 输入数据 (72 个字, 低一位有效, 每个字代表
// 一个 bit)
// 输出参数：
// pBitsOut: FEC 后数据（3 个字, 共 48bits）
short int NVOC_FecDecoder(short int *pWordsIn, short int *pBitsOut);

// 说明：声码器解码
// 输入参数：
// pDeDataStruct: 解码缓冲变量地址
// pBitsIn: 解码输入数据 （3 个字, 2400 速率共 48bits
// 2200 速率共 44bits, 第三个字的低 4bits 可用于加密标识）
// Rate: 0: 2400 1: 2200
// ChanState: fec 解码状态
// 				BIT15：丢帧标志 1：丢帧 0：未丢帧
// 				BIT14：CNI 标志 1：插入舒适噪声 0：不插入
// 输出参数：
// ToneDtmf: 双音多频或者单音频率数值，对应关系如下：
// 			0~15 表示双音多频数值对应 0,1,2,3,4,5,6,7,8,9,a,b,c,d,*,#
// 			300~3500 表示单音频率数值
// pPcmOut: 解码输出语音数据（160 个字, 8K 采样的 PCM 数据）
// 返回参数：
// BIT0：1：单音帧
// BIT1：1：DTMF 帧
// BIT12：1:舒适噪声帧 0:无
// BIT13：1:静音帧
short int NVOC_VoDecoder(short int *pDeDataStruct, short int *pBitsIn, short int *pPcmOut, short int Rate, short int ChanState, short int *ToneDtmf);

// 说明：设置解码参数。目前支持密话提示音相关设置，包括提示音开关、能量、声音持续时间、
// 静音时间和提示音类型设置。
short int NVOC_VoDeCommand(short int *pDeDataStruct, short int Cmd, short int Para);

// 说明：静音帧数据检测。在加密速率下 FEC 解码后调用此函数，如果 ChanState 的静音帧对
// 应标志位置位 1 则说明当前帧是静音帧，不再进行解密工作直接语音解码反之则进行数据解
// 密再语音解码。
short int NVOC_FrameCheck(short int *ChanState,short int *pBitsIn);

// 说明：软解码 FEC 处理 ，提高 FEC 的抗误码性能
// 输入参数：
// Positive1Level: 解调 4FSK 中+1 对应的电平值
// p4FskSoftInfoIn: 软解 FEC 输入电平值 (36 个 Word，对应声码器 72bit 解调的电平值)
// 输出参数：
// pWordsOut: FEC 解码后的数据，3 个字
short int NVOC_SoftFecDecoder(short int Positive1Level,short int *p4FskSoftInfoIn,short int *pWordsOut);

// 说明：生成单音或者 DTMF 的 PCM 数据
// 输入参数:
// pDeDataStruct: 解码结构体
// freametype: 数据帧类型
// FreqDtmfNO:单音频率或者 DTMF 数值
// Level:单音或 DTMF 信号幅度
void NVOC_DTMF_Tone_generate (short int *pDeDataStruct,short int freametype, short int FreqDtmfNO,short int level,short int *pPcmOut);

// 说明：设置 AGC 参数
// 输入参数：
// pEnDataStruct：编码缓冲变量地址
// set_fix_level：目标门限，采样数据的绝对值，1024~16384，默认为 2600（-22dbm0）
// max_db_gain:最大增益，0~24， 默认为 20
// min_db_gain:最小增益，（-24）~0，默认为-6
// 返回值：返回为 0，表示设置失败，为 1 设置正确
short int NVOC_init_agc_dB( short int *pEnDataStruct, short set_fix_level, short max_db_gain, short min_db_gain );