#include <stdio.h>
#include <jni.h>
#include <android/log.h>
#include "ff_tcpip.h"
#include "native_nvoc.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO , "libpcap", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR , "libpcap", __VA_ARGS__)

JNIEXPORT jint JNICALL Java_com_example_pcapdecoder_activity_MainActivity_decode2(JNIEnv *env, jobject thiz, jstring pcap_jstr, jstring audio_jstr, jstring pcm_jstr, jobject object_audioInfo){
	LOGI("decode");
	
	jclass class_audio = (*env)->FindClass(env,"com/example/pcapdecoder/bean/AudioInfo");
	jfieldID fieldID_ip_src = (*env)->GetFieldID(env, class_audio, "ip_src", "I");
	jfieldID fieldID_ip_dst = (*env)->GetFieldID(env, class_audio, "ip_dst", "I");
	jfieldID fieldID_ssrc = (*env)->GetFieldID(env, class_audio, "ssrc", "I");
	jfieldID fieldID_type_num = (*env)->GetFieldID(env, class_audio, "type_num", "I");
	jfieldID fieldID_pkt_count = (*env)->GetFieldID(env, class_audio, "pkt_count", "I");
	jfieldID fieldID_a_line = (*env)->GetFieldID(env, class_audio, "a_line", "Ljava/lang/String;");
	jfieldID fieldID_max_kbps = (*env)->GetFieldID(env, class_audio, "max_kbps", "I");
	jfieldID fieldID_fec = (*env)->GetFieldID(env, class_audio, "fec", "I");
	
	char pcap_str[500]={0};
	char audio_str[500]={0};
	char pcm_str[500]={0};
	u_int32 ip_src = (*env)->GetIntField(env, object_audioInfo, fieldID_ip_src);
    u_int32 ip_dst = (*env)->GetIntField(env, object_audioInfo, fieldID_ip_dst);
    u_int32 ssrc = (*env)->GetIntField(env, object_audioInfo, fieldID_ssrc);
    int type_num = (*env)->GetIntField(env, object_audioInfo, fieldID_type_num);
	int max_kbps = (*env)->GetIntField(env, object_audioInfo, fieldID_max_kbps);
	int fec = (*env)->GetIntField(env, object_audioInfo, fieldID_fec);
	sprintf(pcap_str,"%s",(*env)->GetStringUTFChars(env,pcap_jstr, NULL));
	sprintf(audio_str,"%s",(*env)->GetStringUTFChars(env,audio_jstr, NULL));
	sprintf(pcm_str,"%s",(*env)->GetStringUTFChars(env,pcm_jstr, NULL));
	LOGI("Cpp input pcap_str:%s",pcap_str);
	LOGI("Cpp output audio_str:%s",audio_str);
	LOGI("Cpp output pcm_str:%s",pcm_str);
	LOGI("Cpp ip_src:%d",ip_src);
	LOGI("Cpp ip_dst:%d",ip_dst);
	LOGI("Cpp ssrc:%d",ssrc);
	LOGI("Cpp type_num:%d",type_num);
	LOGI("Cpp max_kbps:%d",max_kbps);
	LOGI("Cpp fec:%d",fec);

	FILE *fileAudio = fopen(audio_str,"rb");
	FILE *filePCM = fopen(pcm_str, "wb");
	
	#define SAMPLERATE 8000
	#define CHANNELS 1
	#define PERIOD_TIME 20 //ms
	#define FRAME_SIZE SAMPLERATE*PERIOD_TIME/1000
	
	#define FRAME_BUFFER_SIZE 2048
	#define VOC_DECODE_BUFFER_SIZE 3
	#define FEC_DECODE_BUFFER_SIZE 72
	
	short int frame_buffer[FRAME_BUFFER_SIZE]; /* 解码用数据大小 */
	short int ChanState; /* fec 解码状态 */
	
	/* 声音解码输入输出数据 */
	u_char audio_buffer[9] = {0};
	short int fec_dec_buffer[FEC_DECODE_BUFFER_SIZE];	/* fec 解码输入数据, 输出数据为 voc_dec_buffer[3] */
	short int voc_dec_buffer[VOC_DECODE_BUFFER_SIZE]; /*	voc 解码输入数据 */
	short int pcm_buffer[FRAME_SIZE]; /* voc 解码输出数据,160个(8K,20ms,单声道)pcm数据，采样位数16 */
	
	int bit_index = 0;
	short int chan_state = 0;
	short int tone_dtmf;
	
	if(NVOC_VoDecoder_Init(frame_buffer, max_kbps)>FRAME_BUFFER_SIZE){
		LOGI("解码缓冲区不够");
		return -1;
	}
	
	while(fread(audio_buffer, sizeof(u_char), 9 ,fileAudio)){
		if(fec == 1){
			for(bit_index = 0; bit_index < FEC_DECODE_BUFFER_SIZE; bit_index = bit_index + 8)
			{
				fec_dec_buffer[bit_index + 0] = ((0x80 & *((u_char *)audio_buffer + bit_index / 8)) !=  0) ?  1 : 0;
				fec_dec_buffer[bit_index + 1] = ((0x40 & *((u_char *)audio_buffer + bit_index / 8)) !=  0) ?  1 : 0;
				fec_dec_buffer[bit_index + 2] = ((0x20 & *((u_char *)audio_buffer + bit_index / 8)) !=  0) ?  1 : 0;
				fec_dec_buffer[bit_index + 3] = ((0x10 & *((u_char *)audio_buffer + bit_index / 8)) !=  0) ?  1 : 0;
				fec_dec_buffer[bit_index + 4] = ((0x08 & *((u_char *)audio_buffer + bit_index / 8)) !=  0) ?  1 : 0;
				fec_dec_buffer[bit_index + 5] = ((0x04 & *((u_char *)audio_buffer + bit_index / 8)) !=  0) ?  1 : 0;
				fec_dec_buffer[bit_index + 6] = ((0x02 & *((u_char *)audio_buffer + bit_index / 8)) !=  0) ?  1 : 0;
				fec_dec_buffer[bit_index + 7] = ((0x01 & *((u_char *)audio_buffer + bit_index / 8)) !=  0) ?  1 : 0;
			}
			chan_state = NVOC_FecDecoder(fec_dec_buffer, voc_dec_buffer);
			NVOC_VoDecoder(frame_buffer, voc_dec_buffer, pcm_buffer, max_kbps, chan_state, &tone_dtmf);
		}
		else{
			memmove(voc_dec_buffer, audio_buffer, 6);
			NVOC_VoDecoder(frame_buffer, voc_dec_buffer, pcm_buffer, max_kbps, chan_state, &tone_dtmf);
		}
		fwrite(pcm_buffer, 2, FRAME_SIZE, filePCM);//Write PCM	
	}
	
	fclose(fileAudio);
	fclose(filePCM);
	LOGI("decode end");
	return 0;
}