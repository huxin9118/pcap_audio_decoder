/*
 *  COPYRIGHT NOTICE
 *  Copyright (C) 2017, Jhuster <huxin9118@163.com>
 *  https://github.com/Jhuster/AudioDemo
 *
 *  @license under the Apache License, Version 2.0
 *
 *  @file    native_audio.c
 *
 *  @author  huxin
 *  @date    2017/10/10
 */
#include <stdio.h>
#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/log.h"
#include "ff_tcpip.h"
#include "opensl_io.h"
#include "native_nvoc.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO , "libpcap", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR , "libpcap", __VA_ARGS__)

#define PCAP_BUFFER_SIZE  1024*128
#define PCAP_BOTTOM_ALERT  1024*32

#define MAX_AUDIO_INFO_SIZE 100
#define MAX_FRAMES_PER_PKT 100

#define SAMPLERATE 8000
#define CHANNELS 1
#define PERIOD_TIME 20 //ms
#define FRAME_SIZE SAMPLERATE*PERIOD_TIME/1000

#define MAX_AUDIO_PACKET_SIZE 2048
#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

#define FRAME_BUFFER_SIZE 2048
#define VOC_DECODE_BUFFER_SIZE 3
#define FEC_DECODE_BUFFER_SIZE 72
#define FEC_FRAME_SIZE 9
#define NO_FEC_FRAME_SIZE 6
//#define FRAMES_PER_PKT 3

u_short switchUshort(u_short s){//16bit大小端转换
	return ((s & 0x00FF) << 8) | ((s & 0xFF00) >> 8);
}

u_int32 switchUint32(u_int32 i){//32bit大小端转换
	return ((i & 0x000000FF) << 24) | ((i & 0x0000FF00) << 8) | ((i & 0x00FF0000) >> 8) | ((i & 0xFF000000) >> 24);
}

int parseAmrFrameType(char* arr,int length, char c,int* index){
	for(int i = 0; i < length; i++){
		if(arr[i] == c){
			*index = i;
			return 0;
		}
	}
	return -1;
}

int pollAudioBuffer(ADUIO_BUFFERS* buffers, char** buffer){
	if(buffers->size > 0){
		*buffer = buffers->data[0];
		//LOGI("====seq:%d\t audio_buffers.size:%d",buffers->seq[0],buffers->size);
		for(int i = 1; i <= buffers->size - 1; i++){
			buffers->data[i-1] = buffers->data[i];
			buffers->seq[i-1] = buffers->seq[i];
		}
		buffers->size--;
		buffers->data[buffers->size] = 0;
		buffers->seq[buffers->size] = 0;
		return 0;
	}
	return -1;
}

int setAudioBuffer(ADUIO_BUFFERS* buffers, char* buffer, int seq){
	if(buffers->size < MAX_AUDIO_BUFFERS_SIZE){
		if(buffers->size == 0){
			buffers->data[0] = buffer;
			buffers->seq[0] = seq;
			buffers->size = 1;
		}
		else{
			int first = 0;
			for(int i = 0; i < buffers->size; i++){
				if(buffers->seq[i] != 0){
					first = i;
					break;
				}
			}
			int index = seq - buffers->seq[first] + first;
			//LOGI("###seq: %5d, index:%5d,",seq,index);
			//LOGI("###first: %5d, findex:%5d,",buffers->seq[first],first);
			if(index < 0){
				return -1;
			}
			buffers->data[index] = buffer;
			buffers->seq[index] = seq;
			if(index+1 >= buffers->size){
				buffers->size = index+1;
			}
		}
		return 0;
	}
	return -1;
}

void clearPCMBuffer(PCM_BUFFERS* buffers){
	if(buffers->size > 0){
		for(int i = 0; i <= buffers->size - 1; i++){
			free(buffers->data[i]);
		}
		buffers->size = 0;
	}
}

int pollPCMBuffer(PCM_BUFFERS* buffers, u_char** buffer){
	if(buffers->size > 0){
		*buffer = buffers->data[0];
		for(int i = 1; i <= buffers->size - 1; i++){
			buffers->data[i-1] = buffers->data[i];
		}
		buffers->data[buffers->size] = 0;
		buffers->size--;
		return 0;
	}
	return -1;
}

int putPCMBuffer(PCM_BUFFERS* buffers, u_char* buffer){
	if(buffers->size < MAX_PCM_BUFFERS_SIZE){
		buffers->data[buffers->size] = buffer;
		buffers->size++;
		return 0;
	}
	return -1;
}

int fill_iobuffer(void* opaque,uint8_t *buf, int bufsize){  
	ADUIO_BUFFERS* buffers = (ADUIO_BUFFERS*)opaque;
	LOGI("seq:%d\t audio_buffers.size:%d",buffers->seq[0],buffers->size);
	if(buffers->hdr_size > 0){
		int hdr_size = buffers->hdr_size;
		//buf = (uint8_t *)buffers->hdr;
		memcpy(buf, buffers->hdr, hdr_size);
		buffers->hdr_size = -1;
		//LOGE("========%d,%d,%s",bufsize,hdr_size,buffers->hdr);
		return hdr_size;
	}	
	else if(buffers->size > 0){
		char* audio_buffer = NULL;
		pollAudioBuffer(buffers,&audio_buffer);
		//buf = (uint8_t *)audio_buffer;
		memcpy(buf, audio_buffer, buffers->buffer_size);
		if(audio_buffer != NULL)
			free(audio_buffer);
		//LOGE("##########%d",buffers->buffer_size);
		return buffers->buffer_size;
	} 
	else{
		//LOGE("+++++++++++");
		return -1;
	}
	// if(!feof(fp_open)){  
		// int true_size=fread(buf,1,bufsize,fp_open);  
		// LOGE("==========%d,%d",bufsize,true_size);
		// return true_size;  
	// }else{  
		// LOGE("+++++++++++");
		// return -1;  
	// }  
}  

int strfind(char *str,char *sub, int str_len, int sub_len){
	//判断字符串长度，然后从第一个开始匹配
	for (int i=0; i<str_len; i++) {
		for (int j=0; j<sub_len; j++) {
			if (*(str+i+j)!=*(sub+j)) {
				break;
			}//如果不相等则跳出这个for循环
			if (j==sub_len - 1) {
				//如果j等于子串的长度则证明已经匹配，则返回子串开始匹配的位置
				return i;
			}
			//若字符相等则开始比较下个字符
		}
	}
	return -1;
}

int getAudioInfosIndex(AUDIO_INFO* audioInfos,int infoSize,u_int32 ip_src,u_int32 ip_dst, u_int32 ssrc){
	for(int i = 0; i<infoSize; i++){
		if(audioInfos[i].ip_src == ip_src && audioInfos[i].ip_dst == ip_dst && audioInfos[i].ssrc == ssrc){
			return i;
		}
	}
	return -1;
}

JNIEXPORT jint JNICALL Java_com_example_pcapdecoder_activity_MainActivity_parsePktInfo(JNIEnv *env, jobject thiz, jstring pcap_jstr, jobject object_list, jobject object_audioInfo){
	LOGI("parsePktInfo start");
	jclass class_list = (*env)->FindClass(env,"java/util/ArrayList");
	jmethodID methodID_list_add = (*env)->GetMethodID(env,class_list,"add","(Ljava/lang/Object;)Z");
	
	jclass class_pkt = (*env)->FindClass(env,"com/example/pcapdecoder/bean/PktInfo");
	jmethodID methodID_pktInfo_constructor = (*env)->GetMethodID(env,class_pkt,"<init>","()V");
	jobject object_pktInfo;
	jfieldID fieldID_frame = (*env)->GetFieldID(env, class_pkt, "frame", "I");
	jfieldID fieldID_seq = (*env)->GetFieldID(env, class_pkt, "seq", "S");
	jfieldID fieldID_timestamp = (*env)->GetFieldID(env, class_pkt, "timestamp", "I");
	jfieldID fieldID_status = (*env)->GetFieldID(env, class_pkt, "status", "I");
	
	jclass class_audio = (*env)->FindClass(env,"com/example/pcapdecoder/bean/AudioInfo");
	jfieldID fieldID_ip_src = (*env)->GetFieldID(env, class_audio, "ip_src", "I");
	jfieldID fieldID_ip_dst = (*env)->GetFieldID(env, class_audio, "ip_dst", "I");
	jfieldID fieldID_ssrc = (*env)->GetFieldID(env, class_audio, "ssrc", "I");
	jfieldID fieldID_type_num = (*env)->GetFieldID(env, class_audio, "type_num", "I");
	jfieldID fieldID_a_line = (*env)->GetFieldID(env, class_audio, "a_line", "Ljava/lang/String;");
	jfieldID fieldID_pkt_count = (*env)->GetFieldID(env, class_audio, "pkt_count", "I");
	jfieldID fieldID_max_kbps = (*env)->GetFieldID(env, class_audio, "max_kbps", "I");
	jfieldID fieldID_fec = (*env)->GetFieldID(env, class_audio, "fec", "I");
	char a_line[10]={0};
	u_int32 ip_src = (*env)->GetIntField(env, object_audioInfo, fieldID_ip_src);
    u_int32 ip_dst = (*env)->GetIntField(env, object_audioInfo, fieldID_ip_dst);
    u_int32 ssrc = (*env)->GetIntField(env, object_audioInfo, fieldID_ssrc);
    int type_num = (*env)->GetIntField(env, object_audioInfo, fieldID_type_num);
	sprintf(a_line,"%s",(*env)->GetStringUTFChars(env,(*env)->GetObjectField(env, object_audioInfo, fieldID_a_line), NULL));
	int pkt_count = (*env)->GetIntField(env, object_audioInfo, fieldID_pkt_count);
	int max_kbps = (*env)->GetIntField(env, object_audioInfo, fieldID_max_kbps);
	int fec = (*env)->GetIntField(env, object_audioInfo, fieldID_fec);
	
		
	char *pcap_buffer = (char*)malloc(PCAP_BUFFER_SIZE);
	char *pos;
	u_int32 pcap_file_linktype; //pcap帧类型
	u_int32 len_pkt_left,  //包的剩余长度
			len_read ,
			len_read_left, //剩余长度
			read_to_end=0 , //是否读取到文件尾
			len_alert = PCAP_BOTTOM_ALERT; //警戒长度，当操作指针距离缓冲区末尾小于这个值的时候，就将剩余部分移动到缓冲区头
		
	PCAP_FILE_HDR *pcap_file_header;
	PCAP_PKT_HDR *pcap_pkt_hdr;
	MAC_FRAME_HDR *frame_hdr;
	LINUX_COOKED_CAPTURE_HDR *linux_cooked_capture_hdr;
	IP_HDR *ip_hdr;
	UDP_HDR *udp_hdr;
	RTP_HDR *rtp_hdr;
	RTP_EXTENSION_HDR *rtp_ex_hdr;

	char sip_request_line[10] ={0x49, 0x4e, 0x56, 0x49, 0x54, 0x45, 0x20, 0x73, 0x69, 0x70};
	char sip_status_line[7] = {0x53, 0x49, 0x50, 0x2f, 0x32, 0x2e, 0x30};
	int sip_request_line_len = 10;
	int sip_status_line_len = 7;

	char a_line_nvoc[5] = {0x20, 0x4e, 0x56, 0x4f, 0x43}; 
	int a_line_nvoc_len = 5; 
	char a_line_nvoc_max_kbps[10] = {0x20, 0x6d, 0x61, 0x78, 0x2d, 0x6b, 0x62, 0x70, 0x73, 0x3d}; 
	int a_line_nvoc_max_kbps_len = 10; 
	char a_line_nvoc_fec[5] = {0x3b, 0x66, 0x65, 0x63, 0x3d}; 
	int a_line_nvoc_fec_len = 5; 
	char nvoc_type_num[10] = {0};
	char nvoc_max_kbps[5] = {0};
	char nvoc_fec[5] = {0};

	char a_line_amr[4] = {0x20, 0x41, 0x4d, 0x52}; 
	int a_line_amr_len = 4; 
	char amr_type_num[10] = {0};
	
	char a_line_ptime[8] = {0x61, 0x3d, 0x70, 0x74, 0x69, 0x6d, 0x65, 0x3a};
	int a_line_ptime_len = 8;
	char amr_ptime[10] = {0};
	char nvoc_ptime[10] = {0};

	int sdp_index = 0;
	
	char rtp_packer_first = 0x80;
	char rtp_extension_packer_first = 0x90;
	int rtp_extension_hdr_size = 0;
	
	int frame_index = 0;
	char pcap_str[500]={0};
	sprintf(pcap_str,"%s",(*env)->GetStringUTFChars(env,pcap_jstr, NULL));
	LOGI("Cpp input pcap_str:%s",pcap_str);
	LOGI("Cpp ip_src:%d",ip_src);
	LOGI("Cpp ip_dst:%d",ip_dst);
	LOGI("Cpp ssrc:%d",ssrc);
	LOGI("Cpp type_num:%d",type_num);
	LOGI("Cpp a_line:%s",a_line);
	LOGI("Cpp pkt_count:%d",pkt_count);
	LOGI("Cpp max_kbps:%d",max_kbps);
	LOGI("Cpp fec:%d",fec);
	
	FILE *filePCAP = fopen(pcap_str,"rb");
	if(filePCAP == NULL)
	{
		LOGE("open err!!!!");
		return -1;
	}
	
	int process = 0;
	pcap_file_header = (PCAP_FILE_HDR*)malloc(sizeof(PCAP_FILE_HDR));
	fread(pcap_file_header, 1, sizeof(PCAP_FILE_HDR), filePCAP);
	//偏移PCAP文件头
	process += sizeof(PCAP_FILE_HDR);
	
	LOGI("pcap_file_header->magic=%u",pcap_file_header->magic);
	int is_pcap_file_header_endian_switch = 0;
	if(pcap_file_header->magic == 0xA1B2C3D4){//pcap文件头标识
		is_pcap_file_header_endian_switch = 0;
	}
	else if(pcap_file_header->magic == 0xD4C3B2A1){
		is_pcap_file_header_endian_switch = 1;
	}
	else{
		LOGE("not pcap file!!!!");
		return -2;
	}	
	LOGI("is_pcap_file_header_endian_switch=%d",is_pcap_file_header_endian_switch);
	
	pos = pcap_buffer;
	len_read = fread(pcap_buffer, sizeof(char), PCAP_BUFFER_SIZE ,filePCAP);
	while(1){		//如果文件读到底了，就全解析完。 否则缓冲区剩余字节小于PCAP_BOTTOM_ALERT的时候退出循环
		while( pos - pcap_buffer <  len_read - len_alert )
		{
			pcap_pkt_hdr = (PCAP_PKT_HDR *)pos;
			if(!is_pcap_file_header_endian_switch){
				len_pkt_left = pcap_pkt_hdr->caplen;
			}
			else{
				len_pkt_left = switchUint32(pcap_pkt_hdr->caplen);
			}
			if(len_pkt_left > len_read -(pos- pcap_buffer)){
				LOGI("break: frame_index=%5d len_pkt_left=%d len_read=%5d pos- pcap_buffer=%5d",frame_index,len_pkt_left,len_read,(pos- pcap_buffer));
				break;
			}
			pos += sizeof(PCAP_PKT_HDR);

			//pos += (sizeof(MAC_FRAME_HDR) + sizeof(IP_HDR) +  sizeof(UDP_HDR));
			//len_pkt_left -=  (sizeof(MAC_FRAME_HDR) + sizeof(IP_HDR) +  sizeof(UDP_HDR));

			if(!is_pcap_file_header_endian_switch){
				pcap_file_linktype = pcap_file_header->linktype;
			}
			else{
				pcap_file_linktype = switchUint32(pcap_file_header->linktype);
			}
			if(pcap_file_linktype == 1){
				//LOGI("MAC_FRAME_HDR: pcap_file_linktype=%d",pcap_file_linktype);
				frame_hdr = (MAC_FRAME_HDR *)pos;
				len_pkt_left -= sizeof(MAC_FRAME_HDR);
				pos += sizeof(MAC_FRAME_HDR);
				frame_index++;
			}else if(pcap_file_linktype == 113){
				//LOGI("LINUX_COOKED_CAPTURE_HDR: pcap_file_linktype=%d",pcap_file_linktype);
				linux_cooked_capture_hdr = (LINUX_COOKED_CAPTURE_HDR *)pos;
				len_pkt_left -= sizeof(LINUX_COOKED_CAPTURE_HDR);
				pos += sizeof(LINUX_COOKED_CAPTURE_HDR);
				frame_index++;
			}
			else{
				//LOGE("unknown: pcap_file_linktype=%6d",pcap_file_linktype);
			}

			if((pcap_file_linktype == 1 && switchUshort(frame_hdr->m_cType) == 0x0800) ||
				(pcap_file_linktype == 113 && switchUshort(linux_cooked_capture_hdr->protocol) == 0x0800)){//数据链路帧的网络层为IPv4(0x0800)
				ip_hdr = (IP_HDR *)pos;
				len_pkt_left -= sizeof(IP_HDR);
				pos += sizeof(IP_HDR);

				if(ip_hdr->ip_protocol == 17){//ip的传输层为UPD
					udp_hdr = (UDP_HDR *)pos;
					len_pkt_left -= sizeof(UDP_HDR);
					pos += sizeof(UDP_HDR);
				
					if(*(pos) == rtp_packer_first || *(pos) == rtp_extension_packer_first){//UDP负载为RTP
						rtp_hdr = (RTP_HDR*)pos;
						len_pkt_left -= sizeof(RTP_HDR);
						pos += sizeof(RTP_HDR);
						
						//pos 指向payload
						len_pkt_left -= switchUshort(rtp_hdr->csrc_count) * sizeof(u_int32);
						pos += switchUshort(rtp_hdr->csrc_count) * sizeof(u_int32);
						if(rtp_hdr->extension == 0x01){
							rtp_ex_hdr = (RTP_EXTENSION_HDR*)pos;
							rtp_extension_hdr_size = sizeof(RTP_EXTENSION_HDR)+ switchUshort(rtp_ex_hdr->extension_length) *sizeof(u_int32);
							len_pkt_left -= rtp_extension_hdr_size;
							pos += rtp_extension_hdr_size;
						}

						if(rtp_hdr->payload_type == type_num && !strcmp(a_line,"AMR")){//RTP负载为AMR-NB
							if(switchUint32(rtp_hdr->ssrc) == ssrc && switchUint32(ip_hdr->ip_source) == ip_src 
							&& switchUint32(ip_hdr->ip_dest) == ip_dst){//该AMR-NB包来自同一同步信源
								object_pktInfo = (*env)->NewObject(env,class_pkt,methodID_pktInfo_constructor);
								(*env)->SetIntField(env, object_pktInfo, fieldID_frame, frame_index);
								(*env)->SetShortField(env, object_pktInfo, fieldID_seq, switchUshort(rtp_hdr->seq));
								(*env)->SetIntField(env, object_pktInfo, fieldID_timestamp, switchUint32(rtp_hdr->timestamp));
								(*env)->CallBooleanMethod(env,object_list,methodID_list_add,object_pktInfo);
								(*env)->DeleteLocalRef(env,object_pktInfo);
							}
						}
						if(rtp_hdr->payload_type == type_num && !strcmp(a_line,"NVOC")){//RTP负载为NVOC
							if(switchUint32(rtp_hdr->ssrc) == ssrc && switchUint32(ip_hdr->ip_source) == ip_src 
							&& switchUint32(ip_hdr->ip_dest) == ip_dst){//该NVOC包来自同一同步信源
								object_pktInfo = (*env)->NewObject(env,class_pkt,methodID_pktInfo_constructor);
								(*env)->SetIntField(env, object_pktInfo, fieldID_frame, frame_index);
								(*env)->SetShortField(env, object_pktInfo, fieldID_seq, switchUshort(rtp_hdr->seq));
								(*env)->SetIntField(env, object_pktInfo, fieldID_timestamp, switchUint32(rtp_hdr->timestamp));
								(*env)->CallBooleanMethod(env,object_list,methodID_list_add,object_pktInfo);
								(*env)->DeleteLocalRef(env,object_pktInfo);							
							}
						}
					}
				}
			}
			//pos 指向下一个pcap 包头的地址
			pos += len_pkt_left;
		}
		if(read_to_end){
			printf("==%d\n",pos - pcap_buffer + process);
			break;
		}
		len_read_left = len_read -(pos- pcap_buffer);
		printf("==%d\n",pos - pcap_buffer + process);
		process += pos- pcap_buffer;
		LOGI("len_read_left [%d]\n", len_read_left);
		//将剩余部分移动到缓冲区头 然后继续从文件中读取 PCAP_BUFFER_SIZE - len_read_left 这么长
		memmove(pcap_buffer,pos,len_read_left);
		pos = pcap_buffer + len_read_left;
		len_read = fread(pos, sizeof(char), (PCAP_BUFFER_SIZE - len_read_left),filePCAP);
		if(len_read < PCAP_BUFFER_SIZE - len_read_left )  //如果读到文件尾，就把警戒值置0，让下一个循环读完
		{
			read_to_end = 1;
			len_alert = 0;
		}
		//待处理的长度为  剩余部分 + 新读取部分
		len_read += len_read_left;
		pos = pcap_buffer;
	}
	fclose(filePCAP);
	free(pcap_file_header);
	free(pcap_buffer);
	LOGI("parsePktInfo end");
	return 0;
}

JNIEXPORT jint JNICALL Java_com_example_pcapdecoder_activity_MainActivity_parseAudioInfo(JNIEnv *env, jobject thiz, jstring pcap_jstr, jint jparse_paramter_type, jobject object_list){
	LOGI("parseAudioInfo start");
	jclass class_list = (*env)->FindClass(env,"java/util/ArrayList");
	//jmethodID methodID_list_constructor = (*env)->GetMethodID(env,class_list,"<init>","()V");
	jmethodID methodID_list_add = (*env)->GetMethodID(env,class_list,"add","(Ljava/lang/Object;)Z");
	jmethodID methodID_list_remove = (*env)->GetMethodID(env,class_list,"remove","(I)Ljava/lang/Object;");
	//jobject object_list= (*env)->NewObject(env,class_list,methodID_list_constructor);
	
	jclass class_audio = (*env)->FindClass(env,"com/example/pcapdecoder/bean/AudioInfo");
	jmethodID methodID_audioInfo_constructor = (*env)->GetMethodID(env,class_audio,"<init>","()V");
	jobject object_audioInfo;
	
	jfieldID fieldID_ip_src = (*env)->GetFieldID(env, class_audio, "ip_src", "I");
	jfieldID fieldID_ip_dst = (*env)->GetFieldID(env, class_audio, "ip_dst", "I");
	jfieldID fieldID_ssrc = (*env)->GetFieldID(env, class_audio, "ssrc", "I");
	jfieldID fieldID_type_num = (*env)->GetFieldID(env, class_audio, "type_num", "I");
	jfieldID fieldID_pkt_count = (*env)->GetFieldID(env, class_audio, "pkt_count", "I");
	jfieldID fieldID_a_line = (*env)->GetFieldID(env, class_audio, "a_line", "Ljava/lang/String;");
	jfieldID fieldID_max_kbps = (*env)->GetFieldID(env, class_audio, "max_kbps", "I");
	jfieldID fieldID_fec = (*env)->GetFieldID(env, class_audio, "fec", "I");
	jfieldID fieldID_ptime = (*env)->GetFieldID(env, class_audio, "ptime", "I");
		
	char *pcap_buffer = (char*)malloc(PCAP_BUFFER_SIZE);
	char *pos;
	u_int32 pcap_file_linktype; //pcap帧类型
	u_int32 len_pkt_left,  //包的剩余长度
			len_read ,
			len_read_left, //剩余长度
			read_to_end=0 , //是否读取到文件尾
			len_alert = PCAP_BOTTOM_ALERT; //警戒长度，当操作指针距离缓冲区末尾小于这个值的时候，就将剩余部分移动到缓冲区头
		
	PCAP_FILE_HDR *pcap_file_header;
	PCAP_PKT_HDR *pcap_pkt_hdr;
	MAC_FRAME_HDR *frame_hdr;
	LINUX_COOKED_CAPTURE_HDR *linux_cooked_capture_hdr;
	IP_HDR *ip_hdr;
	UDP_HDR *udp_hdr;
	RTP_HDR *rtp_hdr;
	RTP_EXTENSION_HDR *rtp_ex_hdr;

	char sip_request_line[10] ={0x49, 0x4e, 0x56, 0x49, 0x54, 0x45, 0x20, 0x73, 0x69, 0x70};
	char sip_status_line[7] = {0x53, 0x49, 0x50, 0x2f, 0x32, 0x2e, 0x30};
	int sip_request_line_len = 10;
	int sip_status_line_len = 7;

	char a_line_nvoc[5] = {0x20, 0x4e, 0x56, 0x4f, 0x43}; 
	int a_line_nvoc_len = 5; 
	char a_line_nvoc_max_kbps[10] = {0x20, 0x6d, 0x61, 0x78, 0x2d, 0x6b, 0x62, 0x70, 0x73, 0x3d}; 
	int a_line_nvoc_max_kbps_len = 10; 
	char a_line_nvoc_fec[5] = {0x3b, 0x66, 0x65, 0x63, 0x3d}; 
	int a_line_nvoc_fec_len = 5; 
	char nvoc_type_num[10] = {0};
	char nvoc_max_kbps[5] = {0};
	char nvoc_fec = 1;

	char a_line_amr[4] = {0x20, 0x41, 0x4d, 0x52}; 
	int a_line_amr_len = 4; 
	char amr_type_num[10] = {0};
	
	char a_line_ptime[8] = {0x61, 0x3d, 0x70, 0x74, 0x69, 0x6d, 0x65, 0x3a};
	int a_line_ptime_len = 8;
	char amr_ptime[10] = {0};
	char nvoc_ptime[10] = {0};
	
	LOGI("parse_paramter_type: %d",jparse_paramter_type);
	if(jparse_paramter_type == 1){
		object_audioInfo = (*env)->CallObjectMethod(env,object_list,methodID_list_remove,0);
		
		jint custom_amr_type_num = (*env)->GetIntField(env, object_audioInfo, fieldID_type_num);
		sprintf(amr_type_num,"%d", custom_amr_type_num);
		LOGI("custom_amr_type_num: %d",custom_amr_type_num);

		jint custom_amr_ptime = (*env)->GetIntField(env, object_audioInfo, fieldID_ptime);
		sprintf(amr_ptime,"%d", custom_amr_ptime);
		LOGI("custom_amr_ptime: %d",custom_amr_ptime);
		
		object_audioInfo = (*env)->CallObjectMethod(env,object_list,methodID_list_remove,0);
		
		jint custom_nvoc_type_num = (*env)->GetIntField(env, object_audioInfo, fieldID_type_num);
		sprintf(nvoc_type_num,"%d", custom_nvoc_type_num);
		LOGI("custom_nvoc_type_num: %d",custom_nvoc_type_num);

		jint custom_nvoc_ptime = (*env)->GetIntField(env, object_audioInfo, fieldID_ptime);
		sprintf(nvoc_ptime,"%d", custom_nvoc_ptime);
		LOGI("custom_nvoc_ptime: %d",custom_nvoc_ptime);
		
		jint custom_nvoc_max_kbps = (*env)->GetIntField(env, object_audioInfo, fieldID_max_kbps);
		if(custom_nvoc_max_kbps == 0){
			sprintf(nvoc_max_kbps,"%s", "2.4");
			LOGI("custom_nvoc_max_kbps: %s", "2.4");
		}
		else if(custom_nvoc_max_kbps == 1){
			LOGI("custom_nvoc_max_kbps: %s", "2.2");
		}
		
		jint custom_nvoc_fec = (*env)->GetIntField(env, object_audioInfo, fieldID_fec);
		nvoc_fec = custom_nvoc_fec;
		LOGI("custom_nvoc_fec: %d",custom_nvoc_fec);
		
		object_audioInfo = NULL;
	}

	int sdp_index = 0;
	
	char rtp_packer_first = 0x80;
	char rtp_extension_packer_first = 0x90;
	int rtp_extension_hdr_size = 0;
	AUDIO_INFO audioInfos[MAX_AUDIO_INFO_SIZE];
	int infoSize = 0;
	int infoIndex = 0;
	
	int frame_index = 0;
	char pcap_str[500]={0};
	sprintf(pcap_str,"%s",(*env)->GetStringUTFChars(env,pcap_jstr, NULL));
	LOGI("Cpp input pcap_str:%s",pcap_str);
	
	FILE *filePCAP = fopen(pcap_str,"rb");
	if(filePCAP == NULL)
	{
		LOGE("open err!!!!");
		return -1;
	}
	
	int process = 0;
	pcap_file_header = (PCAP_FILE_HDR*)malloc(sizeof(PCAP_FILE_HDR));
	fread(pcap_file_header, 1, sizeof(PCAP_FILE_HDR), filePCAP);
	//偏移PCAP文件头
	process += sizeof(PCAP_FILE_HDR);
	
	LOGI("pcap_file_header->magic=%u",pcap_file_header->magic);
	int is_pcap_file_header_endian_switch = 0;
	if(pcap_file_header->magic == 0xA1B2C3D4){//pcap文件头标识
		is_pcap_file_header_endian_switch = 0;
	}
	else if(pcap_file_header->magic == 0xD4C3B2A1){
		is_pcap_file_header_endian_switch = 1;
	}
	else{
		LOGE("not pcap file!!!!");
		return -2;
	}	
	LOGI("is_pcap_file_header_endian_switch=%d",is_pcap_file_header_endian_switch);
	
	pos = pcap_buffer;
	len_read = fread(pcap_buffer, sizeof(char), PCAP_BUFFER_SIZE ,filePCAP);
	while(1){		//如果文件读到底了，就全解析完。 否则缓冲区剩余字节小于PCAP_BOTTOM_ALERT的时候退出循环
		while( pos - pcap_buffer <  len_read - len_alert )
		{
			pcap_pkt_hdr = (PCAP_PKT_HDR *)pos;
			if(!is_pcap_file_header_endian_switch){
				len_pkt_left = pcap_pkt_hdr->caplen;
			}
			else{
				len_pkt_left = switchUint32(pcap_pkt_hdr->caplen);
			}
			if(len_pkt_left > len_read -(pos- pcap_buffer)){
				LOGI("break: frame_index=%5d len_pkt_left=%d len_read=%5d pos- pcap_buffer=%5d",frame_index,len_pkt_left,len_read,(pos- pcap_buffer));
				break;
			}
			pos += sizeof(PCAP_PKT_HDR);

			//pos += (sizeof(MAC_FRAME_HDR) + sizeof(IP_HDR) +  sizeof(UDP_HDR));
			//len_pkt_left -=  (sizeof(MAC_FRAME_HDR) + sizeof(IP_HDR) +  sizeof(UDP_HDR));
			
			if(!is_pcap_file_header_endian_switch){
				pcap_file_linktype = pcap_file_header->linktype;
			}
			else{
				pcap_file_linktype = switchUint32(pcap_file_header->linktype);
			}
			if(pcap_file_linktype == 1){
				//LOGI("MAC_FRAME_HDR: pcap_file_linktype=%d",pcap_file_linktype);
				frame_hdr = (MAC_FRAME_HDR *)pos;
				len_pkt_left -= sizeof(MAC_FRAME_HDR);
				pos += sizeof(MAC_FRAME_HDR);
				frame_index++;
			}else if(pcap_file_linktype == 113){
				//LOGI("LINUX_COOKED_CAPTURE_HDR: pcap_file_linktype=%d",pcap_file_linktype);
				linux_cooked_capture_hdr = (LINUX_COOKED_CAPTURE_HDR *)pos;
				len_pkt_left -= sizeof(LINUX_COOKED_CAPTURE_HDR);
				pos += sizeof(LINUX_COOKED_CAPTURE_HDR);
				frame_index++;
			}
			else{
				LOGE("unknown: pcap_file_linktype=%6d",pcap_file_linktype);
			}

			if((pcap_file_linktype == 1 && switchUshort(frame_hdr->m_cType) == 0x0800) ||
				(pcap_file_linktype == 113 && switchUshort(linux_cooked_capture_hdr->protocol) == 0x0800)){//数据链路帧的网络层为IPv4(0x0800)
				ip_hdr = (IP_HDR *)pos;
				len_pkt_left -= sizeof(IP_HDR);
				pos += sizeof(IP_HDR);

				if(ip_hdr->ip_protocol == 17){//ip的传输层为UPD
					udp_hdr = (UDP_HDR *)pos;
					len_pkt_left -= sizeof(UDP_HDR);
					pos += sizeof(UDP_HDR);
					
					if(!strlen(amr_type_num)){
						// if(strfind(pos, sip_request_line, sip_request_line_len, sip_request_line_len)!=-1){
							// sdp_index = strfind(pos, a_line_amr, switchUshort(udp_hdr->length)-8, a_line_amr_len);
							// if(sdp_index != -1){
								// for(int i = sdp_index - 1; i>sdp_index - 10; i--){
									// if(*(pos+i) < 0x30 || *(pos+i) > 0x39){
										// if(i < sdp_index - 1){
											// memmove(amr_type_num,pos+i+1,sdp_index - 1 - i);
											// break;
										// }
									// }
								// }
							// }			
							// if(strlen(amr_type_num)){
								// sdp_index = strfind(pos, a_line_ptime, switchUshort(udp_hdr->length)-8, a_line_ptime_len);
								// if(sdp_index != -1){
									// for(int i = sdp_index + a_line_ptime_len; i < sdp_index + 10; i++){
										// if(*(pos+i) < 0x30 || *(pos+i) > 0x39){
											// break;
										// }else{
											// memmove(amr_ptime,pos+i,1);	
										// }
									// }
								// }
							// }
						// }
						if(strfind(pos, sip_status_line, sip_status_line_len, sip_status_line_len)!=-1){
							sdp_index = strfind(pos, a_line_amr, switchUshort(udp_hdr->length)-8, a_line_amr_len);
							if(sdp_index != -1){
								for(int i = sdp_index - 1; i>sdp_index - 10; i--){
									if(*(pos+i) < 0x30 || *(pos+i) > 0x39){
										if(i < sdp_index - 1){
											memmove(amr_type_num,pos+i+1,sdp_index - 1 - i);
											break;
										}
									}
								}
							}
							if(strlen(amr_type_num)){
								sdp_index = strfind(pos, a_line_ptime, switchUshort(udp_hdr->length)-8, a_line_ptime_len);
								if(sdp_index != -1){
									for(int i = sdp_index + a_line_ptime_len; i < sdp_index + 10; i++){
										if(*(pos+i) < 0x30 || *(pos+i) > 0x39){
											break;
										}else{
											memmove(amr_ptime+strlen(amr_ptime),pos+i,1);	
										}
									}
								}
							}
						}
					}

					if(!strlen(nvoc_type_num)){
						// if(strfind(pos, sip_request_line, sip_request_line_len, sip_request_line_len)!=-1){
							// sdp_index = strfind(pos, a_line_nvoc, switchUshort(udp_hdr->length)-8, a_line_nvoc_len);
							// if(sdp_index != -1){
								// for(int i = sdp_index - 1; i>sdp_index - 10; i--){
									// if(*(pos+i) < 0x30 || *(pos+i) > 0x39){
										// if(i < sdp_index - 1){
											// memmove(nvoc_type_num,pos+i+1,sdp_index - 1 - i);
											// break;
										// }
									// }
								// }
							// }
							// if(strlen(nvoc_type_num)){
								// sdp_index = strfind(pos, a_line_nvoc_max_kbps, switchUshort(udp_hdr->length)-8, a_line_nvoc_max_kbps_len);
								// if(sdp_index != -1){
									// memmove(nvoc_max_kbps,pos+sdp_index+a_line_nvoc_max_kbps_len,3);	
								// }
								// sdp_index = strfind(pos, a_line_nvoc_fec, switchUshort(udp_hdr->length)-8, a_line_nvoc_fec_len);
								// if(sdp_index != -1){
									// memmove(&nvoc_fec,pos+sdp_index+a_line_nvoc_fec_len,1);	
								// }
								// sdp_index = strfind(pos, a_line_ptime, switchUshort(udp_hdr->length)-8, a_line_ptime_len);
								// if(sdp_index != -1){
									// for(int i = sdp_index + a_line_ptime_len; i < sdp_index + 10; i++){
										// if(*(pos+i) < 0x30 || *(pos+i) > 0x39){
											// break;
										// }else{
											// memmove(nvoc_ptime,pos+i,1);	
										// }
									// }
								// }
							// }
						// }
						if(strfind(pos, sip_status_line, sip_status_line_len, sip_status_line_len)!=-1){
							sdp_index = strfind(pos, a_line_nvoc, switchUshort(udp_hdr->length)-8, a_line_nvoc_len);
							if(sdp_index != -1){
								for(int i = sdp_index - 1; i>sdp_index - 10; i--){
									if(*(pos+i) < 0x30 || *(pos+i) > 0x39){
										if(i < sdp_index - 1){
											memmove(nvoc_type_num,pos+i+1,sdp_index - 1 - i);
											break;
										}
									}
								}
							}
							if(strlen(nvoc_type_num)){
								sdp_index = strfind(pos, a_line_nvoc_max_kbps, switchUshort(udp_hdr->length)-8, a_line_nvoc_max_kbps_len);
								if(sdp_index != -1){
									memmove(nvoc_max_kbps,pos+sdp_index+a_line_nvoc_max_kbps_len,3);	
								}
								sdp_index = strfind(pos, a_line_nvoc_fec, switchUshort(udp_hdr->length)-8, a_line_nvoc_fec_len);
								if(sdp_index != -1){
									if(*(pos+sdp_index+a_line_nvoc_fec_len) == 0x30 || *(pos+sdp_index+a_line_nvoc_fec_len) == 0x31){
										nvoc_fec = atoi(pos+sdp_index+a_line_nvoc_fec_len);
									}
								}
								sdp_index = strfind(pos, a_line_ptime, switchUshort(udp_hdr->length)-8, a_line_ptime_len);
								if(sdp_index != -1){
									for(int i = sdp_index + a_line_ptime_len; i < sdp_index + 10; i++){
										if(*(pos+i) < 0x30 || *(pos+i) > 0x39){
											break;
										}else{
											memmove(nvoc_ptime+strlen(nvoc_ptime),pos+i,1);	
										}
									}
								}
							}
						}
					}
					
					if(*(pos) == rtp_packer_first || *(pos) == rtp_extension_packer_first){//UDP负载为RTP
						rtp_hdr = (RTP_HDR*)pos;
						len_pkt_left -= sizeof(RTP_HDR);
						pos += sizeof(RTP_HDR);

						//pos 指向payload
						len_pkt_left -= switchUshort(rtp_hdr->csrc_count) * sizeof(u_int32);
						pos += switchUshort(rtp_hdr->csrc_count) * sizeof(u_int32);
						if(rtp_hdr->extension == 0x01){
							rtp_ex_hdr = (RTP_EXTENSION_HDR*)pos;
							rtp_extension_hdr_size = sizeof(RTP_EXTENSION_HDR)+ switchUshort(rtp_ex_hdr->extension_length) *sizeof(u_int32);
							len_pkt_left -= rtp_extension_hdr_size;
							pos += rtp_extension_hdr_size;
						}
						
						if(rtp_hdr->payload_type == atoi(nvoc_type_num) || rtp_hdr->payload_type == atoi(amr_type_num)){
							infoIndex = getAudioInfosIndex(audioInfos,infoSize,switchUint32(ip_hdr->ip_source),switchUint32(ip_hdr->ip_dest),switchUint32(rtp_hdr->ssrc));
							if(infoIndex == -1){
								audioInfos[infoSize].ip_src = switchUint32(ip_hdr->ip_source);
								audioInfos[infoSize].ip_dst = switchUint32(ip_hdr->ip_dest);
								audioInfos[infoSize].ssrc = switchUint32(rtp_hdr->ssrc);
								if(rtp_hdr->payload_type == atoi(nvoc_type_num)){
									audioInfos[infoSize].type_num = atoi(nvoc_type_num);
									memmove(audioInfos[infoSize].a_line,a_line_nvoc+1,a_line_nvoc_len-1);
									audioInfos[infoSize].a_line[a_line_nvoc_len-1] = 0;
									
									if(nvoc_max_kbps[2]=='4'){
										audioInfos[infoSize].max_kbps = 0;
									}
									else if(nvoc_max_kbps[2]=='2'){
										audioInfos[infoSize].max_kbps = 1;
									}
									else{
										audioInfos[infoSize].max_kbps = 0;
									}
									audioInfos[infoSize].fec = nvoc_fec;
									audioInfos[infoSize].ptime = atoi(nvoc_ptime);
									if(audioInfos[infoSize].ptime == 0){
										audioInfos[infoSize].ptime = PERIOD_TIME * 3;
									}
								}
								else if(rtp_hdr->payload_type == atoi(amr_type_num)){
									audioInfos[infoSize].type_num = atoi(amr_type_num);
									memmove(audioInfos[infoSize].a_line,a_line_amr+1,a_line_amr_len-1);
									audioInfos[infoSize].a_line[a_line_amr_len-1] = 0;
									audioInfos[infoSize].ptime = atoi(amr_ptime);
									if(audioInfos[infoSize].ptime == 0){
										audioInfos[infoSize].ptime = PERIOD_TIME;
									}
								}
								audioInfos[infoSize].pkt_count = 1;
								infoSize++;
							}
							else{
								audioInfos[infoIndex].pkt_count++;
							}
						}
					}
				}
			}
			//pos 指向下一个pcap 包头的地址
			pos += len_pkt_left;
		}
		if(read_to_end){
			printf("==%d\n",pos - pcap_buffer + process);
			break;
		}
		len_read_left = len_read -(pos- pcap_buffer);
		printf("==%d\n",pos - pcap_buffer + process);
		process += pos- pcap_buffer;
		LOGI("len_read_left [%d]\n", len_read_left);
		//将剩余部分移动到缓冲区头 然后继续从文件中读取 PCAP_BUFFER_SIZE - len_read_left 这么长
		memmove(pcap_buffer,pos,len_read_left);
		pos = pcap_buffer + len_read_left;
		len_read = fread(pos, sizeof(char), (PCAP_BUFFER_SIZE - len_read_left),filePCAP);
		if(len_read < PCAP_BUFFER_SIZE - len_read_left )  //如果读到文件尾，就把警戒值置0，让下一个循环读完
		{
			read_to_end = 1;
			len_alert = 0;
		}
		//待处理的长度为  剩余部分 + 新读取部分
		len_read += len_read_left;
		pos = pcap_buffer;
	}
	fclose(filePCAP);
	free(pcap_file_header);
	free(pcap_buffer);
	for(int i = 0; i<infoSize; i++){
		object_audioInfo = (*env)->NewObject(env,class_audio,methodID_audioInfo_constructor);
		(*env)->SetIntField(env, object_audioInfo, fieldID_ip_src, audioInfos[i].ip_src);
		(*env)->SetIntField(env, object_audioInfo, fieldID_ip_dst, audioInfos[i].ip_dst);
		(*env)->SetIntField(env, object_audioInfo, fieldID_ssrc, audioInfos[i].ssrc);
		(*env)->SetIntField(env, object_audioInfo, fieldID_type_num, audioInfos[i].type_num);
		(*env)->SetIntField(env, object_audioInfo, fieldID_pkt_count, audioInfos[i].pkt_count);
		(*env)->SetObjectField(env, object_audioInfo, fieldID_a_line, (*env)->NewStringUTF(env,audioInfos[i].a_line));
		(*env)->SetIntField(env, object_audioInfo, fieldID_max_kbps, audioInfos[i].max_kbps);
		(*env)->SetIntField(env, object_audioInfo, fieldID_fec, audioInfos[i].fec);
		(*env)->SetIntField(env, object_audioInfo, fieldID_ptime, audioInfos[i].ptime);
		
		(*env)->CallBooleanMethod(env,object_list,methodID_list_add,object_audioInfo);
		(*env)->DeleteLocalRef(env,object_audioInfo);
	}
	LOGI("parseAudioInfo end");
	return 0;
}

typedef struct info_str{
	char * pcap_str;
	char * audio_str;
	char * pcm_str;
	u_int32 ip_src;
	u_int32 ip_dst;
	u_int32 ssrc;
	char * a_line;
	int type_num;
	int pkt_count;
	int max_kbps;
	int fec;
	int ptime;
	JNIEnv* env;
} INFO_STR;

static volatile int parse_method_type = 0;
static volatile int parse_exit = 0;
static int returnValue = 1;
static PCM_BUFFERS pcm_buffers;
static JavaVM* gs_jvm;
static jclass gs_class;
static pthread_mutex_t mutex;

void *decode(void *argv)
{
	LOGI("decode start");
	returnValue = 1;
	char *pcap_buffer = (char*)malloc(PCAP_BUFFER_SIZE);
	char *pos;
	u_int32 pcap_file_linktype; //pcap帧类型
	u_int32 len_pkt_left,  //包的剩余长度
			len_read ,
			len_read_left, //剩余长度
			read_to_end=0 , //是否读取到文件尾
			len_alert = PCAP_BOTTOM_ALERT; //警戒长度，当操作指针距离缓冲区末尾小于这个值的时候，就将剩余部分移动到缓冲区头
		
	PCAP_FILE_HDR *pcap_file_header;
	PCAP_PKT_HDR *pcap_pkt_hdr;
	MAC_FRAME_HDR *frame_hdr;
	LINUX_COOKED_CAPTURE_HDR *linux_cooked_capture_hdr;
	IP_HDR *ip_hdr;
	UDP_HDR *udp_hdr;
	RTP_HDR *rtp_hdr;
	RTP_EXTENSION_HDR *rtp_ex_hdr;

	char amr_nb_type[8]= {0x04, 0x0c, 0x14, 0x1c, 0x24, 0x2c, 0x34, 0x3c};
	char amr_nb_type_collect[8]= {0x84, 0x8c, 0x94, 0x9c, 0xa4, 0xac, 0xb4, 0xbc};
	int amr_nb_size[8] = {13,   14,   16 ,  18,   19,   21,   26,   32};
	char amr_wb_type[9]= {0x04, 0x0c, 0x14, 0x1c, 0x24, 0x2c, 0x34, 0x3c, 0x44};
	int amr_wb_size[9] = {18,   23,   33,   37,   41,   47,   51,   59,   61};

	char rtp_packer_first = 0x80;
	char rtp_extension_packer_first = 0x90;
	int rtp_extension_hdr_size = 0;
	u_short rtp_seq = 0;
	char amr_frame_first = 0xf0;
	char amr_nb_hdr[6] = {0x23, 0x21, 0x41, 0x4d, 0x52, 0x0a};
	int amr_type_index[MAX_FRAMES_PER_PKT];
	int amr_buffer_size = 0;
	int nvoc_buffer_size = 0;

	ADUIO_BUFFERS audio_buffers;
	memset(&audio_buffers, 0, sizeof(ADUIO_BUFFERS));
	memset(&pcm_buffers, 0, sizeof(PCM_BUFFERS));
	char* audio_buffer;
	pcm_buffers.size = 0;
	u_char* pcm_buffer;

	INFO_STR* info_str = (INFO_STR *)argv;
	int frame_index = 0;
	FILE *filePCAP = fopen(info_str->pcap_str,"rb");
	FILE *fileAudio = fopen(info_str->audio_str,"wb");
	FILE *filePCM = NULL;
	if(parse_method_type != 2){
		filePCM = fopen(info_str->pcm_str, "wb");
	}
	
	if(parse_method_type != 2){
		if(filePCAP == NULL || fileAudio==NULL || filePCM == NULL){
			LOGE("open err!!!!");
			returnValue = -1;
			return &returnValue;
		}
	}
	else{
		if(filePCAP == NULL || fileAudio==NULL){
			LOGE("open err!!!!");
			returnValue = -1;
			return &returnValue;
		}
	}

	const int frames_per_pkt = info_str->ptime / PERIOD_TIME;
	//--------------------------------------------------------------------------------------------
	AVFormatContext	*pFormatCtx;
	AVDictionary *options;
	int				audioStream;
	AVCodecContext	*pCodecCtx;
	AVCodec			*pCodec;
	AVPacket		*packet;
	AVFrame			*pFrame;
	int ret;
	uint32_t len = 0;
	int got_picture;
	int audio_index = 0;
	int pcm_index = 0;
	int64_t in_channel_layout;
	struct SwrContext *au_convert_ctx;
	int swr_size;

	AVIOContext *avio;
	AVInputFormat *pInputFormat;
	av_register_all();
	avformat_network_init();
	pFormatCtx = avformat_alloc_context();
	uint8_t* iobuffer=(uint8_t *)av_malloc(MAX_AUDIO_PACKET_SIZE);  

	uint64_t out_channel_layout;
	int out_nb_samples;
	enum AVSampleFormat out_sample_fmt;
	int out_sample_rate;
	int out_channels;
	uint8_t* swr_buffer=(uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
	//--------------------------------------------------------------------------------------------
	short int frame_buffer[FRAME_BUFFER_SIZE] = {0}; /* 解码用数据大小 */
	short int ChanState; /* fec 解码状态 */
	
	short int fec_dec_buffer[FEC_DECODE_BUFFER_SIZE] = {0};	/* fec 解码输入数据, 输出数据为 voc_dec_buffer[3] */
	short int voc_dec_buffer[VOC_DECODE_BUFFER_SIZE] = {0}; 	/* voc 解码输入数据 */
	//short int pcm_dec_buffer[FRAME_SIZE * frames_per_pkt] = {0}; /* voc 解码输出数据,160个(8K,20ms,单声道)pcm数据，采样位数16 */
	short int *pcm_dec_buffer; /* voc 解码输出数据,160个(8K,20ms,单声道)pcm数据，采样位数16 */
	pcm_dec_buffer = (short int *)malloc(sizeof(short int) * FRAME_SIZE * frames_per_pkt);
	memset(pcm_dec_buffer,0,sizeof(short int) * FRAME_SIZE * frames_per_pkt);
	
	int bit_index = 0;
	short int chan_state = 0;
	short int tone_dtmf;
	//--------------------------------------------------------------------------------------------

	long process = 0;

	JNIEnv *env;
	if(parse_method_type == 1){
		LOGI("AttachCurrentThread");
		(*gs_jvm)->AttachCurrentThread(gs_jvm,&env,NULL);
	}
	else{
		env = info_str->env;
	}

	jmethodID methodID_setProgressRate = (*env)->GetStaticMethodID(env, gs_class, "setProgressRate", "(I)V");
	pcap_file_header = (PCAP_FILE_HDR*)malloc(sizeof(PCAP_FILE_HDR));
	fread(pcap_file_header, 1, sizeof(PCAP_FILE_HDR), filePCAP);
	//偏移PCAP文件头
	process += sizeof(PCAP_FILE_HDR);
	
	LOGI("pcap_file_header->magic=%u",pcap_file_header->magic);
	int is_pcap_file_header_endian_switch = 0;
	if(pcap_file_header->magic == 0xA1B2C3D4){//pcap文件头标识
		is_pcap_file_header_endian_switch = 0;
	}
	else if(pcap_file_header->magic == 0xD4C3B2A1){
		is_pcap_file_header_endian_switch = 1;
	}
	else{
		LOGE("not pcap file!!!!");
		returnValue = -2;
		return &returnValue;
	}	
	LOGI("is_pcap_file_header_endian_switch=%d",is_pcap_file_header_endian_switch);
	
	pos = pcap_buffer;
	len_read = fread(pcap_buffer, sizeof(char), PCAP_BUFFER_SIZE ,filePCAP);
	while(!parse_exit){		//如果文件读到底了，就全解析完。 否则缓冲区剩余字节小于PCAP_BOTTOM_ALERT的时候退出循环
		while( pos - pcap_buffer <  len_read - len_alert ){
			if(pcm_buffers.size < MAX_PCM_BUFFERS_SIZE / 2 || parse_method_type != 1){
				pcap_pkt_hdr = (PCAP_PKT_HDR *)pos;
				if(!is_pcap_file_header_endian_switch){
					len_pkt_left = pcap_pkt_hdr->caplen;
				}
				else{
					len_pkt_left = switchUint32(pcap_pkt_hdr->caplen);
				}
				if(len_pkt_left > len_read -(pos- pcap_buffer)){
					LOGI("break: frame_index=%5d len_pkt_left=%d len_read=%5d pos- pcap_buffer=%5d",frame_index,len_pkt_left,len_read,(pos- pcap_buffer));
					break;
				}
				pos += sizeof(PCAP_PKT_HDR);

				//pos += (sizeof(MAC_FRAME_HDR) + sizeof(IP_HDR) +  sizeof(UDP_HDR));
				//len_pkt_left -=  (sizeof(MAC_FRAME_HDR) + sizeof(IP_HDR) +  sizeof(UDP_HDR));

				if(!is_pcap_file_header_endian_switch){
					pcap_file_linktype = pcap_file_header->linktype;
				}
				else{
					pcap_file_linktype = switchUint32(pcap_file_header->linktype);
				}
				if(pcap_file_linktype == 1){
					//LOGI("MAC_FRAME_HDR: pcap_file_linktype=%d",pcap_file_linktype);
					frame_hdr = (MAC_FRAME_HDR *)pos;
					len_pkt_left -= sizeof(MAC_FRAME_HDR);
					pos += sizeof(MAC_FRAME_HDR);
					frame_index++;
				}else if(pcap_file_linktype == 113){
					//LOGI("LINUX_COOKED_CAPTURE_HDR: pcap_file_linktype=%d",pcap_file_linktype);
					linux_cooked_capture_hdr = (LINUX_COOKED_CAPTURE_HDR *)pos;
					len_pkt_left -= sizeof(LINUX_COOKED_CAPTURE_HDR);
					pos += sizeof(LINUX_COOKED_CAPTURE_HDR);
					frame_index++;
				}
				else{
					//LOGE("unknown: pcap_file_linktype=%6d",pcap_file_linktype);
				}

				if((pcap_file_linktype == 1 && switchUshort(frame_hdr->m_cType) == 0x0800) ||
					(pcap_file_linktype == 113 && switchUshort(linux_cooked_capture_hdr->protocol) == 0x0800)){//数据链路帧的网络层为IPv4(0x0800)
					ip_hdr = (IP_HDR *)pos;
					len_pkt_left -= sizeof(IP_HDR);
					pos += sizeof(IP_HDR);

					if(ip_hdr->ip_protocol == 17){//ip的传输层为UPD
						udp_hdr = (UDP_HDR *)pos;
						len_pkt_left -= sizeof(UDP_HDR);
						pos += sizeof(UDP_HDR);
						
						if(*(pos) == rtp_packer_first || *(pos) == rtp_extension_packer_first){//UDP负载为RTP
							rtp_hdr = (RTP_HDR*)pos;
							len_pkt_left -= sizeof(RTP_HDR);
							pos += sizeof(RTP_HDR);
							
							//pos 指向payload
							len_pkt_left -= switchUshort(rtp_hdr->csrc_count) * sizeof(u_int32);
							pos += switchUshort(rtp_hdr->csrc_count) * sizeof(u_int32);
							if(rtp_hdr->extension == 0x01){
								rtp_ex_hdr = (RTP_EXTENSION_HDR*)pos;
								rtp_extension_hdr_size = sizeof(RTP_EXTENSION_HDR)+ switchUshort(rtp_ex_hdr->extension_length) *sizeof(u_int32);
								len_pkt_left -= rtp_extension_hdr_size;
								pos += rtp_extension_hdr_size;
							}

							if(rtp_hdr->payload_type == info_str->type_num && !strcmp(info_str->a_line,"AMR")){//RTP负载为AMR-NB
								if(switchUint32(rtp_hdr->ssrc) == info_str->ssrc && switchUint32(ip_hdr->ip_source) == info_str->ip_src 
								&& switchUint32(ip_hdr->ip_dest) == info_str->ip_dst){//该AMR-NB包来自同一同步信源
									if(rtp_seq == 0){
										fwrite(amr_nb_hdr, 1,6, fileAudio);
										audio_buffers.hdr_size = 6;
										audio_buffers.hdr = (char *)malloc(6);
										memmove(audio_buffers.hdr,amr_nb_hdr,6);
										rtp_seq = switchUshort(rtp_hdr->seq);

										avio =avio_alloc_context(iobuffer, MAX_AUDIO_PACKET_SIZE,0,&audio_buffers,fill_iobuffer,NULL,NULL);  
										pFormatCtx->pb=avio; 

										//char* input = "/storage/emulated/0/0testAudio/000.amr";
										if(avformat_open_input(&pFormatCtx,"",NULL,NULL)!=0){
											LOGE("Couldn't open input stream.");
											returnValue = -2;
											return &returnValue;
										}
										// Retrieve stream information
										if(avformat_find_stream_info(pFormatCtx,NULL)<0){
											LOGE("Couldn't find stream information.");
											returnValue = -3;
											return &returnValue;
										}

										// Find the first audio stream
										audioStream=-1;
										for(int i=0; i < pFormatCtx->nb_streams; i++){
											if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO){
												audioStream=i;
												break;
											}
										}

										if(audioStream==-1){
											LOGE("Didn't find a audio stream.");
											returnValue = -4;
											return &returnValue;
										}

										// Get a pointer to the codec context for the audio stream
										pCodecCtx=pFormatCtx->streams[audioStream]->codec;

										// Find the decoder for the audio stream
										pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
										if(pCodec==NULL){
											LOGE("Codec not found.");
											returnValue = -5;
											return &returnValue;
										}

										// Open codec
										if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
											LOGE("Could not open codec.");
											returnValue = -6;
											return &returnValue;
										}

										packet=(AVPacket *)av_malloc(sizeof(AVPacket));
										av_init_packet(packet);

										//Out Audio Param
										out_channel_layout = pCodecCtx->channel_layout;
										//nb_samples: AAC-1024 MP3-1152 AMR_NB-160:8000*0.02
										out_nb_samples=160;
										out_sample_fmt = AV_SAMPLE_FMT_S16;//AMR:16bit
										out_sample_rate = pCodecCtx->sample_rate;//AMR-nb:8k, AMR-wb:16k
										out_channels = pCodecCtx->channels;//单通道：1
										//out_channels=av_get_channel_layout_nb_channels(out_channel_layout);
										pFrame=av_frame_alloc();

										//FIX:Some Codec's Context Information is missing
										//in_channel_layout=av_get_default_channel_layout(pCodecCtx->channels);

										//Swr
										au_convert_ctx = swr_alloc();
										au_convert_ctx = swr_alloc_set_opts(au_convert_ctx,out_channel_layout, AV_SAMPLE_FMT_S16, out_sample_rate,
											pCodecCtx->channel_layout, pCodecCtx->sample_fmt , pCodecCtx->sample_rate,0, NULL);
										swr_init(au_convert_ctx);
										//--------------------------------------------------------------------------------------------------------------
									}else{
										rtp_seq++;
									}

									if(*(pos) == amr_frame_first ){
										for(int i = 0; i<frames_per_pkt; i++){
											if(i < frames_per_pkt - 1 && parseAmrFrameType(amr_nb_type_collect,8,*(pos+1+i),&amr_type_index[i])){
												returnValue = -7;
												return &returnValue;
											}
											if(i == frames_per_pkt - 1 && parseAmrFrameType(amr_nb_type,8,*(pos+1+i),&amr_type_index[i])){
												returnValue = -7;
												return &returnValue;
											}
										}
										
										if(parse_method_type == 2){
											LOGI("rtp payload length:%d", switchUshort(udp_hdr->length) - 8 - (int)((void*)pos - (void*)rtp_hdr));
											fwrite(pos, 1,switchUshort(udp_hdr->length) - 8 - (int)((void*)pos - (void*)rtp_hdr), fileAudio);
										}else{
											if(switchUshort(udp_hdr->length) - 8 - (int)((void*)pos - (void*)rtp_hdr) < amr_buffer_size * frames_per_pkt){
												LOGI("rtp payload is to short!!!rtp payload len:%d\t amr_buffer_size :%d\t frame_index:%d", switchUshort(udp_hdr->length) - 8 - (int)((void*)pos - (void*)rtp_hdr), amr_buffer_size * frames_per_pkt,frame_index);
												//pos 指向下一个pcap 包头的地址
												pos += len_pkt_left;
												continue;
											}
											
											if(frames_per_pkt == 1){
												amr_buffer_size = amr_nb_size[amr_type_index[0]];
												fwrite(pos+1, 1,amr_buffer_size, fileAudio);
												if(audio_buffers.buffer_size == 0){
													audio_buffers.buffer_size = amr_buffer_size;
												}
												audio_buffer = (char *)malloc(1*amr_buffer_size);
												memmove(audio_buffer,pos+1,amr_buffer_size);
												setAudioBuffer(&audio_buffers,audio_buffer,switchUshort(rtp_hdr->seq));
												audio_index++;
												LOGI("audio_buffers.size:%5d\t audio_index:%5d\t pkt_count:%5d\t seq:%5d",audio_buffers.size,audio_index,info_str->pkt_count,switchUshort(rtp_hdr->seq));
											}
											else if(frames_per_pkt > 1){
												amr_buffer_size = amr_nb_size[amr_type_index[frames_per_pkt - 1]];
												if(audio_buffers.buffer_size == 0){
													audio_buffers.buffer_size = amr_buffer_size * frames_per_pkt;
												}
												audio_buffer = (char *)malloc(1*audio_buffers.buffer_size);
												
												for(int i = 0; i<frames_per_pkt; i++){
													fwrite(pos + frames_per_pkt, 1,1, fileAudio);
													fwrite(pos + frames_per_pkt + 1 + (amr_buffer_size - 1) * i, 1,amr_buffer_size - 1, fileAudio);
													
													memmove(audio_buffer + amr_buffer_size * i,pos + frames_per_pkt,1);
													memmove(audio_buffer + amr_buffer_size * i + 1, pos + frames_per_pkt + 1 + (amr_buffer_size - 1) * i, amr_buffer_size - 1);
												}
												setAudioBuffer(&audio_buffers,audio_buffer,switchUshort(rtp_hdr->seq));
												audio_index++;
												LOGI("audio_buffers.size:%5d\t audio_index:%5d\t pkt_count:%5d seq:%5d",audio_buffers.size,audio_index,info_str->pkt_count,switchUshort(rtp_hdr->seq));
											}
											// LOGI("%5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d",audio_buffers.seq[0],audio_buffers.seq[1],audio_buffers.seq[2],audio_buffers.seq[3],audio_buffers.seq[4]
											// ,audio_buffers.seq[5],audio_buffers.seq[6],audio_buffers.seq[7],audio_buffers.seq[8],audio_buffers.seq[9],audio_buffers.seq[10],audio_buffers.seq[11],audio_buffers.seq[12]
											// ,audio_buffers.seq[13],audio_buffers.seq[14],audio_buffers.seq[15],audio_buffers.seq[16],audio_buffers.seq[17],audio_buffers.seq[18],audio_buffers.seq[19]);
											
											if(audio_buffers.size >= 20 || audio_index >= info_str->pkt_count){
											//--------------------------------------------------------------------------------------------------------------
												while(audio_buffers.size > 10 || (audio_buffers.size > 0 && audio_index >= info_str->pkt_count)){
													//LOGI("seq:%d\t audio_buffers.size:%d",audio_buffers.seq[0],audio_buffers.size);
													
													if(audio_buffers.seq[0] != 0){
														if(av_read_frame(pFormatCtx, packet)>=0){
															if(packet->stream_index==audioStream){
																ret = avcodec_decode_audio4( pCodecCtx, pFrame,&got_picture, packet);
																if ( ret < 0 ) {
																	LOGE("Error in decoding audio frame.");
																	returnValue = -8;
																	return &returnValue;
																}
																if ( got_picture > 0 ){
																	// LOGI("%5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d",audio_buffers.seq[0],audio_buffers.seq[1],audio_buffers.seq[2],audio_buffers.seq[3],audio_buffers.seq[4]
																	// ,audio_buffers.seq[5],audio_buffers.seq[6],audio_buffers.seq[7],audio_buffers.seq[8],audio_buffers.seq[9],audio_buffers.seq[10],audio_buffers.seq[11],audio_buffers.seq[12]
																	// ,audio_buffers.seq[13],audio_buffers.seq[14],audio_buffers.seq[15],audio_buffers.seq[16],audio_buffers.seq[17],audio_buffers.seq[18],audio_buffers.seq[19]);
																	swr_size = swr_convert(au_convert_ctx,&swr_buffer, MAX_AUDIO_FRAME_SIZE,(const uint8_t **)pFrame->data , out_nb_samples);
																	//pcm_buffer_size=av_samples_get_buffer_size(NULL,out_channels ,pFrame->nb_samples, out_sample_fmt, 1);
																	if(out_sample_fmt == AV_SAMPLE_FMT_U8){
																		pcm_buffers.buffer_size = swr_size * out_channels  * 1;
																	}
																	else if(out_sample_fmt == AV_SAMPLE_FMT_S16){
																		pcm_buffers.buffer_size = swr_size * out_channels  * 2;
																	}
																	else if(out_sample_fmt == AV_SAMPLE_FMT_FLT){
																		pcm_buffers.buffer_size = swr_size * out_channels  * 4;
																	}
																	fwrite(swr_buffer, 1, pcm_buffers.buffer_size, filePCM);//Write PCM	
																	
																	pcm_buffer=(u_char *)malloc(1*pcm_buffers.buffer_size);
																	memmove(pcm_buffer,swr_buffer,pcm_buffers.buffer_size);
																	pcm_index++;
																	LOGI("pcm_index:%5d\t pts:%lld\t packet size:%d\t pcm_buffers.size:%d\t audio_buffers.size:%d",pcm_index,packet->pts,packet->size,pcm_buffers.size,audio_buffers.size);
																	//################获取mutex#################
																	if(parse_method_type == 1 && 0 != pthread_mutex_lock(&mutex)){
																		returnValue = -9;
																		return &returnValue;
																	}	
																	//##########################################
																	putPCMBuffer(&pcm_buffers, pcm_buffer);
																	//################释放mutex#################
																	if(parse_method_type == 1 && 0 != pthread_mutex_unlock(&mutex)){
																		returnValue = -10;
																		return &returnValue;
																	}
																	//##########################################
																}
															}
														}
														av_free_packet(packet);
													}
													else{
														LOGI("pkt loss: %d", audio_buffers.seq[1]-1);
														pollAudioBuffer(&audio_buffers,&audio_buffer);
														pcm_buffer=(u_char *)malloc(1*pcm_buffers.buffer_size);
														memset(pcm_buffer, 0, 1*pcm_buffers.buffer_size);
														fwrite(pcm_buffer, 1, pcm_buffers.buffer_size, filePCM);//Write PCM	
														pcm_index++;
														LOGI("loss pcm_index:%5d\t packet size:%d  %d",pcm_index,pcm_buffers.buffer_size,pcm_buffers.size);
														//################获取mutex#################
														if(parse_method_type == 1 && 0 != pthread_mutex_lock(&mutex)){
															returnValue = -9;
															return &returnValue;
														}	
														//##########################################
														putPCMBuffer(&pcm_buffers, pcm_buffer);
														//################释放mutex#################
														if(parse_method_type == 1 && 0 != pthread_mutex_unlock(&mutex)){
															returnValue = -10;
															return &returnValue;
														}
														//##########################################
													}
												}
												//--------------------------------------------------------------------------------------------------------------
											}
										}
									}								
								}
							}
							if(rtp_hdr->payload_type == info_str->type_num && !strcmp(info_str->a_line,"NVOC")){//RTP负载为NVOC
								if(switchUint32(rtp_hdr->ssrc) == info_str->ssrc && switchUint32(ip_hdr->ip_source) == info_str->ip_src 
								&& switchUint32(ip_hdr->ip_dest) == info_str->ip_dst){//该NVOC包来自同一同步信源
									if(rtp_seq == 0){
										rtp_seq = switchUshort(rtp_hdr->seq);
										
										if(NVOC_VoDecoder_Init(frame_buffer, info_str->max_kbps)>FRAME_BUFFER_SIZE){
											LOGI("解码缓冲区不够");
											returnValue = -11;
											return &returnValue;
										}
									}else{
										rtp_seq++;
									}
									
									if(info_str->fec == 1){
										nvoc_buffer_size = FEC_FRAME_SIZE;
									}
									else{
										nvoc_buffer_size = NO_FEC_FRAME_SIZE;
									}
									
									if(parse_method_type == 2){
										LOGI("rtp payload length:%d", switchUshort(udp_hdr->length) - 8 - (int)((void*)pos - (void*)rtp_hdr));
										fwrite(pos, 1,switchUshort(udp_hdr->length) - 8 - (int)((void*)pos - (void*)rtp_hdr), fileAudio);
									}else{
										//LOGI("%d====%d====%d", (void*)pos - (void*)rtp_hdr, switchUshort(udp_hdr->length) - 8,frame_index);
										if(switchUshort(udp_hdr->length) - 8 - (int)((void*)pos - (void*)rtp_hdr) < nvoc_buffer_size * frames_per_pkt){
											LOGI("rtp payload is to short!!!rtp payload len:%d\t nvoc_buffer_size :%d\t frame_index:%d", switchUshort(udp_hdr->length) - 8 - (int)((void*)pos - (void*)rtp_hdr), nvoc_buffer_size * frames_per_pkt,frame_index);
											//pos 指向下一个pcap 包头的地址
											pos += len_pkt_left;
											continue;
										}
										
										fwrite(pos, 1,nvoc_buffer_size * frames_per_pkt, fileAudio);	
										if(audio_buffers.buffer_size == 0){
											audio_buffers.buffer_size = nvoc_buffer_size * frames_per_pkt;
										}
										audio_buffer = (char *)malloc(1*audio_buffers.buffer_size);
										memmove(audio_buffer,pos,audio_buffers.buffer_size);
										setAudioBuffer(&audio_buffers,audio_buffer,switchUshort(rtp_hdr->seq));
										audio_index++;
										LOGI("audio_buffers.size:%5d\t audio_index:%5d\t pkt_count:%5d",audio_buffers.size,audio_index,info_str->pkt_count);

										if(audio_buffers.size >= 20 || audio_index >= info_str->pkt_count){
											//--------------------------------------------------------------------------------------------------------------
											while(audio_buffers.size > 10 || (audio_buffers.size > 0 && audio_index >= info_str->pkt_count)){
												LOGI("seq:%d\t audio_buffers.size:%d",audio_buffers.seq[0],audio_buffers.size);
												if(audio_buffers.seq[0] != 0){
													pollAudioBuffer(&audio_buffers,&audio_buffer);
													for(int i = 0; i < frames_per_pkt; i++){
														if(info_str->fec == 1){
															for(bit_index = 0; bit_index < FEC_DECODE_BUFFER_SIZE; bit_index = bit_index + 8)
															{
																fec_dec_buffer[bit_index + 0] = ((0x80 & *((u_char *)audio_buffer + bit_index / 8 + i * nvoc_buffer_size)) !=  0) ?  1 : 0;
																fec_dec_buffer[bit_index + 1] = ((0x40 & *((u_char *)audio_buffer + bit_index / 8 + i * nvoc_buffer_size)) !=  0) ?  1 : 0;
																fec_dec_buffer[bit_index + 2] = ((0x20 & *((u_char *)audio_buffer + bit_index / 8 + i * nvoc_buffer_size)) !=  0) ?  1 : 0;
																fec_dec_buffer[bit_index + 3] = ((0x10 & *((u_char *)audio_buffer + bit_index / 8 + i * nvoc_buffer_size)) !=  0) ?  1 : 0;
																fec_dec_buffer[bit_index + 4] = ((0x08 & *((u_char *)audio_buffer + bit_index / 8 + i * nvoc_buffer_size)) !=  0) ?  1 : 0;
																fec_dec_buffer[bit_index + 5] = ((0x04 & *((u_char *)audio_buffer + bit_index / 8 + i * nvoc_buffer_size)) !=  0) ?  1 : 0;
																fec_dec_buffer[bit_index + 6] = ((0x02 & *((u_char *)audio_buffer + bit_index / 8 + i * nvoc_buffer_size)) !=  0) ?  1 : 0;
																fec_dec_buffer[bit_index + 7] = ((0x01 & *((u_char *)audio_buffer + bit_index / 8 + i * nvoc_buffer_size)) !=  0) ?  1 : 0;
																//LOGI("%d,%d,%d,%d,%d,%d,%d,%d",fec_dec_buffer[bit_index + 0],fec_dec_buffer[bit_index + 1],fec_dec_buffer[bit_index + 2],fec_dec_buffer[bit_index + 3],
																//fec_dec_buffer[bit_index + 4],fec_dec_buffer[bit_index + 5],fec_dec_buffer[bit_index + 6],fec_dec_buffer[bit_index + 7]);
															}
															chan_state = NVOC_FecDecoder(fec_dec_buffer, voc_dec_buffer);
															NVOC_VoDecoder(frame_buffer, voc_dec_buffer, pcm_dec_buffer + i * FRAME_SIZE, info_str->max_kbps, chan_state, &tone_dtmf);
														}
														else{
															memmove(voc_dec_buffer, audio_buffer, audio_buffers.buffer_size);
															NVOC_VoDecoder(frame_buffer, voc_dec_buffer, pcm_dec_buffer + i * FRAME_SIZE, info_str->max_kbps, chan_state, &tone_dtmf);
														}
													}
													//fwrite(pcm_dec_buffer, sizeof(short int), FRAME_SIZE * frames_per_pkt, filePCM);//Write PCM	
													
													for(int i = 0; i < frames_per_pkt; i++){
														pcm_buffers.buffer_size = sizeof(short int) * FRAME_SIZE;
														pcm_buffer=(u_char *)malloc(1*pcm_buffers.buffer_size);
														memmove(pcm_buffer,pcm_dec_buffer + pcm_buffers.buffer_size * i / sizeof(short int),pcm_buffers.buffer_size);
														fwrite(pcm_buffer, 1, pcm_buffers.buffer_size, filePCM);
														pcm_index++;
														LOGI("pcm_index:%5d\t packet size:%d  %d",pcm_index,pcm_buffers.buffer_size,pcm_buffers.size);
														//################获取mutex#################
														if(parse_method_type == 1 && 0 != pthread_mutex_lock(&mutex)){
															returnValue = -9;
															return &returnValue;
														}	
														//##########################################
														putPCMBuffer(&pcm_buffers, pcm_buffer);
														//################释放mutex#################
														if(parse_method_type == 1 && 0 != pthread_mutex_unlock(&mutex)){
															returnValue = -10;
															return &returnValue;
														}
														//##########################################													
													}
													
													// pcm_buffers.buffer_size = sizeof(short int) * FRAME_SIZE * frames_per_pkt;
													// pcm_buffer=(u_char *)malloc(1*pcm_buffers.buffer_size);
													// memmove(pcm_buffer,pcm_dec_buffer,pcm_buffers.buffer_size);
													// pcm_index++;
													// LOGI("pcm_index:%5d\t packet size:%d  %d",pcm_index,pcm_buffers.buffer_size,pcm_buffers.size);
													// //################获取mutex#################
													// if(parse_method_type == 1 && 0 != pthread_mutex_lock(&mutex)){
														// returnValue = -9;
														// return &returnValue;
													// }	
													// //##########################################
													// putPCMBuffer(&pcm_buffers, pcm_buffer);
													// //################释放mutex#################
													// if(parse_method_type == 1 && 0 != pthread_mutex_unlock(&mutex)){
														// returnValue = -10;
														// return &returnValue;
													// }
													// //##########################################	
													free(audio_buffer);	
												}
												else{
													LOGI("pkt loss: %d", audio_buffers.seq[1]);
													pollAudioBuffer(&audio_buffers,&audio_buffer);
													for(int i = 0; i < frames_per_pkt; i++){
														pcm_buffer=(u_char *)malloc(1*pcm_buffers.buffer_size);
														memset(pcm_buffer, 0, 1*pcm_buffers.buffer_size);
														fwrite(pcm_buffer, 1, pcm_buffers.buffer_size, filePCM);//Write PCM	
														pcm_index++;
														LOGI("loss pcm_index:%5d\t packet size:%d  %d",pcm_index,pcm_buffers.buffer_size,pcm_buffers.size);
														//################获取mutex#################
														if(parse_method_type == 1 && 0 != pthread_mutex_lock(&mutex)){
															returnValue = -9;
															return &returnValue;
														}	
														//##########################################
														putPCMBuffer(&pcm_buffers, pcm_buffer);
														//################释放mutex#################
														if(parse_method_type == 1 && 0 != pthread_mutex_unlock(&mutex)){
															returnValue = -10;
															return &returnValue;
														}
														//##########################################	
													}												
												}
											}
											//--------------------------------------------------------------------------------------------------------------
										}
									}
								}
							}
						}
					}
				}
				//pos 指向下一个pcap 包头的地址
				pos += len_pkt_left;
			}
			else{ 
				if(parse_exit){
					break;
				}
				usleep(20000);
			}
		}
		if(read_to_end || parse_exit){
			//printf("==%d\n",pos - pcap_buffer + process);
			LOGI("len_read_left [%d], read_already [%ld]", len_read_left, pos - pcap_buffer + process);
			if(parse_method_type != 1){
				(*env)->CallStaticVoidMethod(env, gs_class, methodID_setProgressRate, pos - pcap_buffer + process);
			}
			break;
		}
		len_read_left = len_read -(pos- pcap_buffer);
		
		process += pos- pcap_buffer;
		//printf("==%d\n",pos - pcap_buffer + process);
		LOGI("len_read_left [%d], read_already [%ld]", len_read_left, pos - pcap_buffer + process);
		if(parse_method_type != 1){
			(*env)->CallStaticVoidMethod(env, gs_class, methodID_setProgressRate, pos - pcap_buffer + process);
		}
		//将剩余部分移动到缓冲区头 然后继续从文件中读取 PCAP_BUFFER_SIZE - len_read_left 这么长
		memmove(pcap_buffer,pos,len_read_left);
		pos = pcap_buffer + len_read_left;
		len_read = fread(pos, sizeof(char), (PCAP_BUFFER_SIZE - len_read_left),filePCAP);
		if(len_read < PCAP_BUFFER_SIZE - len_read_left )  //如果读到文件尾，就把警戒值置0，让下一个循环读完
		{
			read_to_end = 1;
			len_alert = 0;
		}
		//待处理的长度为  剩余部分 + 新读取部分
		len_read += len_read_left;
		pos = pcap_buffer;
	}
	
	LOGI("00000000000000");
	LOGI("11111111111111");
	if(parse_method_type != 2){
		fclose(filePCM);
	}
	fclose(filePCAP);
	fclose(fileAudio);
	free(pcm_dec_buffer);
	free(pcap_file_header);
	free(pcap_buffer);
	if(!strcmp(info_str->a_line,"AMR")){
		LOGI("22222222222222");
		swr_free(&au_convert_ctx);
		LOGI("33333333333333");
		//av_free(iobuffer);
		LOGI("44444444444444");
		av_free(swr_buffer);
		LOGI("55555555555555");
		av_frame_free(&pFrame);
		LOGI("66666666666666");
		// Close the codec
		if(pCodecCtx != NULL)avcodec_close(pCodecCtx);
		LOGI("77777777777777");
		// Close the video file
		if(pFormatCtx != NULL)avformat_close_input(&pFormatCtx);
	}
	LOGI("88888888888888");
	if(parse_method_type == 1){
		LOGI("DetachCurrentThread");
		(*gs_jvm)->DetachCurrentThread(gs_jvm);
	}
	LOGI("decode end");
	returnValue = 0;
	return &returnValue;
}

JNIEXPORT jint JNICALL Java_com_example_pcapdecoder_activity_MainActivity_play(JNIEnv *env, jobject thiz, jstring pcap_jstr, jstring audio_jstr, jstring pcm_jstr, jobject object_audioInfo)
{
	LOGI("play");
	jclass objcetClass = (*env)->FindClass(env,"com/example/pcapdecoder/activity/MainActivity");
	jmethodID methodID_setProgressRateFull = (*env)->GetStaticMethodID(env, objcetClass, "setProgressRateFull", "()V");
	jmethodID methodID_setProgressRateEmpty = (*env)->GetStaticMethodID(env, objcetClass, "setProgressRateEmpty", "()V");
	jmethodID methodID_setProgressRate = (*env)->GetStaticMethodID(env, objcetClass, "setProgressRate", "(I)V");
	
	jclass class_audio = (*env)->FindClass(env,"com/example/pcapdecoder/bean/AudioInfo");
	jfieldID fieldID_ip_src = (*env)->GetFieldID(env, class_audio, "ip_src", "I");
	jfieldID fieldID_ip_dst = (*env)->GetFieldID(env, class_audio, "ip_dst", "I");
	jfieldID fieldID_ssrc = (*env)->GetFieldID(env, class_audio, "ssrc", "I");
	jfieldID fieldID_type_num = (*env)->GetFieldID(env, class_audio, "type_num", "I");
	jfieldID fieldID_a_line = (*env)->GetFieldID(env, class_audio, "a_line", "Ljava/lang/String;");
	jfieldID fieldID_pkt_count = (*env)->GetFieldID(env, class_audio, "pkt_count", "I");
	jfieldID fieldID_max_kbps = (*env)->GetFieldID(env, class_audio, "max_kbps", "I");
	jfieldID fieldID_fec = (*env)->GetFieldID(env, class_audio, "fec", "I");
	jfieldID fieldID_ptime = (*env)->GetFieldID(env, class_audio, "ptime", "I");
	
	char pcap_str[500]={0};
	char audio_str[500]={0};
	char pcm_str[500]={0};
	char a_line[10]={0};
	u_int32 ip_src = (*env)->GetIntField(env, object_audioInfo, fieldID_ip_src);
    u_int32 ip_dst = (*env)->GetIntField(env, object_audioInfo, fieldID_ip_dst);
    u_int32 ssrc = (*env)->GetIntField(env, object_audioInfo, fieldID_ssrc);
    int type_num = (*env)->GetIntField(env, object_audioInfo, fieldID_type_num);
	sprintf(a_line,"%s",(*env)->GetStringUTFChars(env,(*env)->GetObjectField(env, object_audioInfo, fieldID_a_line), NULL));
	int pkt_count = (*env)->GetIntField(env, object_audioInfo, fieldID_pkt_count);
	int max_kbps = (*env)->GetIntField(env, object_audioInfo, fieldID_max_kbps);
	int fec = (*env)->GetIntField(env, object_audioInfo, fieldID_fec);
	int ptime = (*env)->GetIntField(env, object_audioInfo, fieldID_ptime);
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
	LOGI("Cpp a_line:%s",a_line);
	LOGI("Cpp pkt_count:%d",pkt_count);
	LOGI("Cpp max_kbps:%d",max_kbps);
	LOGI("Cpp fec:%d",fec);
	LOGI("Cpp ptime:%d",ptime);

	INFO_STR info_str;
	info_str.pcap_str = pcap_str;
	info_str.audio_str = audio_str;
	info_str.pcm_str = pcm_str;
	info_str.ip_src = ip_src;
	info_str.ip_dst = ip_dst;
	info_str.ssrc = ssrc;
	info_str.type_num = type_num;
	info_str.a_line = a_line;
	info_str.pkt_count = pkt_count;
	info_str.max_kbps = max_kbps;
	info_str.fec = fec;
	info_str.ptime = ptime;
	
	parse_method_type = 1;
	parse_exit = 0;
	//(*env)->GetJavaVM(env,&gs_jvm);
	gs_class=(*env)->NewGlobalRef(env,objcetClass);
	
	if(0 != pthread_mutex_init(&mutex, NULL)){
		return -1;
	}
	
	pthread_t decode_thread;
	pthread_create(&decode_thread, NULL, decode, &info_str);
	do{
		usleep(10000);
	}while(pcm_buffers.size < 10 && returnValue == 1);

    OPENSL_STREAM* stream = android_OpenAudioDevice(SAMPLERATE, CHANNELS, CHANNELS, FRAME_SIZE);
    if (stream == NULL) {
        LOGE("failed to open audio device ! \n");
        return -1;
    }

    int samples;
	u_char* pcm_buffer;
	int result;
	
	int index = 0;
	while (!parse_exit) {
		//################获取mutex#################
		if(0 != pthread_mutex_lock(&mutex)){
			return -1;
		}	
		//##########################################
		result = pollPCMBuffer(&pcm_buffers, &pcm_buffer);
		//################释放mutex#################
		if(0 != pthread_mutex_unlock(&mutex)){
			return -1;
		}
		//##########################################
		if(result){
			LOGI("pull finish,break");
			break;
		}
        samples = android_AudioOut(stream, pcm_buffer, pcm_buffers.buffer_size / 2);
		free(pcm_buffer);
        if (samples < 0) {
            LOGE("android_AudioOut failed !");
        }
		index++;
		(*env)->CallStaticVoidMethod(env, objcetClass, methodID_setProgressRate, index);
        LOGI("[%d]playback %d samples ! %d",index, samples, pcm_buffers.size);
    }
	LOGI("aaaaaaaaaaaaaaaaaa");
    android_CloseAudioDevice(stream);
	LOGI("bbbbbbbbbbbbbbbbbb");
	clearPCMBuffer(&pcm_buffers);
	LOGI("cccccccccccccccccc");
	if(parse_exit == 0){
		LOGI("nativeStartPlayback completed !");
		(*env)->CallStaticVoidMethod(env, objcetClass, methodID_setProgressRateFull);
	}
	else{
		LOGI("nativeStartPlayback Cancel !!!");
		(*env)->CallStaticVoidMethod(env, objcetClass, methodID_setProgressRateEmpty);
	}
	
	if(0 != pthread_mutex_destroy(&mutex)){
		return -1;
	}
	if(gs_class!=NULL)
		(*env)->DeleteGlobalRef(env,gs_class);
    return 0;
}


JNIEXPORT void JNICALL Java_com_example_pcapdecoder_activity_MainActivity_playCancel(JNIEnv *env, jobject thiz)
{
	parse_method_type = 1;
    parse_exit = 1;
}

JNIEXPORT jint JNICALL Java_com_example_pcapdecoder_activity_MainActivity_decode(JNIEnv *env, jobject thiz, jstring pcap_jstr, jstring audio_jstr, jstring pcm_jstr, jobject object_audioInfo)
{
	LOGI("decode");
	jclass objcetClass = (*env)->FindClass(env,"com/example/pcapdecoder/activity/MainActivity");
	jmethodID methodID_setProgressRateFull = (*env)->GetStaticMethodID(env, objcetClass, "setProgressRateFull", "()V");
	jmethodID methodID_setProgressRateEmpty = (*env)->GetStaticMethodID(env, objcetClass, "setProgressRateEmpty", "()V");
	
	jclass class_audio = (*env)->FindClass(env,"com/example/pcapdecoder/bean/AudioInfo");
	jfieldID fieldID_ip_src = (*env)->GetFieldID(env, class_audio, "ip_src", "I");
	jfieldID fieldID_ip_dst = (*env)->GetFieldID(env, class_audio, "ip_dst", "I");
	jfieldID fieldID_ssrc = (*env)->GetFieldID(env, class_audio, "ssrc", "I");
	jfieldID fieldID_type_num = (*env)->GetFieldID(env, class_audio, "type_num", "I");
	jfieldID fieldID_a_line = (*env)->GetFieldID(env, class_audio, "a_line", "Ljava/lang/String;");
	jfieldID fieldID_pkt_count = (*env)->GetFieldID(env, class_audio, "pkt_count", "I");
	jfieldID fieldID_max_kbps = (*env)->GetFieldID(env, class_audio, "max_kbps", "I");
	jfieldID fieldID_fec = (*env)->GetFieldID(env, class_audio, "fec", "I");
	jfieldID fieldID_ptime = (*env)->GetFieldID(env, class_audio, "ptime", "I");
	
	char pcap_str[500]={0};
	char audio_str[500]={0};
	char pcm_str[500]={0};
	char a_line[10]={0};
	u_int32 ip_src = (*env)->GetIntField(env, object_audioInfo, fieldID_ip_src);
    u_int32 ip_dst = (*env)->GetIntField(env, object_audioInfo, fieldID_ip_dst);
    u_int32 ssrc = (*env)->GetIntField(env, object_audioInfo, fieldID_ssrc);
    int type_num = (*env)->GetIntField(env, object_audioInfo, fieldID_type_num);
	sprintf(a_line,"%s",(*env)->GetStringUTFChars(env,(*env)->GetObjectField(env, object_audioInfo, fieldID_a_line), NULL));
	int pkt_count = (*env)->GetIntField(env, object_audioInfo, fieldID_pkt_count);
	int max_kbps = (*env)->GetIntField(env, object_audioInfo, fieldID_max_kbps);
	int fec = (*env)->GetIntField(env, object_audioInfo, fieldID_fec);
	int ptime = (*env)->GetIntField(env, object_audioInfo, fieldID_ptime);
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
	LOGI("Cpp a_line:%s",a_line);
	LOGI("Cpp pkt_count:%d",pkt_count);
	LOGI("Cpp max_kbps:%d",max_kbps);
	LOGI("Cpp fec:%d",fec);
	LOGI("Cpp ptime:%d",ptime);

	INFO_STR info_str;
	info_str.pcap_str = pcap_str;
	info_str.audio_str = audio_str;
	info_str.pcm_str = pcm_str;
	info_str.ip_src = ip_src;
	info_str.ip_dst = ip_dst;
	info_str.ssrc = ssrc;
	info_str.type_num = type_num;
	info_str.a_line = a_line;
	info_str.pkt_count = pkt_count;
	info_str.max_kbps = max_kbps;
	info_str.fec = fec;
	info_str.ptime = ptime;
	
	parse_method_type = 0;
	parse_exit = 0;
	if(parse_method_type != 1){
		info_str.env = env;
	}
	gs_class=(*env)->NewGlobalRef(env,objcetClass);
	decode(&info_str);
	clearPCMBuffer(&pcm_buffers);
	if(parse_exit == 0){
		LOGI("nativeStartPlayback completed !");
		(*env)->CallStaticVoidMethod(env, objcetClass, methodID_setProgressRateFull);
	}
	else{
		LOGI("nativeStartPlayback Cancel !!!");
		(*env)->CallStaticVoidMethod(env, objcetClass, methodID_setProgressRateEmpty);
	}
	if(gs_class!=NULL)
		(*env)->DeleteGlobalRef(env,gs_class);
    return 0;
}


JNIEXPORT void JNICALL Java_com_example_pcapdecoder_activity_MainActivity_decodeCancel(JNIEnv *env, jobject thiz)
{
	parse_method_type = 0;
    parse_exit = 1;
}

JNIEXPORT jint JNICALL Java_com_example_pcapdecoder_activity_MainActivity_getPayload(JNIEnv *env, jobject thiz, jstring pcap_jstr, jstring audio_jstr, jstring pcm_jstr, jobject object_audioInfo)
{
	LOGI("getPayload");
	jclass objcetClass = (*env)->FindClass(env,"com/example/pcapdecoder/activity/MainActivity");
	jmethodID methodID_setProgressRateFull = (*env)->GetStaticMethodID(env, objcetClass, "setProgressRateFull", "()V");
	jmethodID methodID_setProgressRateEmpty = (*env)->GetStaticMethodID(env, objcetClass, "setProgressRateEmpty", "()V");
	
	jclass class_audio = (*env)->FindClass(env,"com/example/pcapdecoder/bean/AudioInfo");
	jfieldID fieldID_ip_src = (*env)->GetFieldID(env, class_audio, "ip_src", "I");
	jfieldID fieldID_ip_dst = (*env)->GetFieldID(env, class_audio, "ip_dst", "I");
	jfieldID fieldID_ssrc = (*env)->GetFieldID(env, class_audio, "ssrc", "I");
	jfieldID fieldID_type_num = (*env)->GetFieldID(env, class_audio, "type_num", "I");
	jfieldID fieldID_a_line = (*env)->GetFieldID(env, class_audio, "a_line", "Ljava/lang/String;");
	jfieldID fieldID_pkt_count = (*env)->GetFieldID(env, class_audio, "pkt_count", "I");
	jfieldID fieldID_max_kbps = (*env)->GetFieldID(env, class_audio, "max_kbps", "I");
	jfieldID fieldID_fec = (*env)->GetFieldID(env, class_audio, "fec", "I");
	jfieldID fieldID_ptime = (*env)->GetFieldID(env, class_audio, "ptime", "I");
	
	char pcap_str[500]={0};
	char audio_str[500]={0};
	char pcm_str[500]={0};
	char a_line[10]={0};
	u_int32 ip_src = (*env)->GetIntField(env, object_audioInfo, fieldID_ip_src);
    u_int32 ip_dst = (*env)->GetIntField(env, object_audioInfo, fieldID_ip_dst);
    u_int32 ssrc = (*env)->GetIntField(env, object_audioInfo, fieldID_ssrc);
    int type_num = (*env)->GetIntField(env, object_audioInfo, fieldID_type_num);
	sprintf(a_line,"%s",(*env)->GetStringUTFChars(env,(*env)->GetObjectField(env, object_audioInfo, fieldID_a_line), NULL));
	int pkt_count = (*env)->GetIntField(env, object_audioInfo, fieldID_pkt_count);
	int max_kbps = (*env)->GetIntField(env, object_audioInfo, fieldID_max_kbps);
	int fec = (*env)->GetIntField(env, object_audioInfo, fieldID_fec);
	int ptime = (*env)->GetIntField(env, object_audioInfo, fieldID_ptime);
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
	LOGI("Cpp a_line:%s",a_line);
	LOGI("Cpp pkt_count:%d",pkt_count);
	LOGI("Cpp max_kbps:%d",max_kbps);
	LOGI("Cpp fec:%d",fec);
	LOGI("Cpp ptime:%d",ptime);

	INFO_STR info_str;
	info_str.pcap_str = pcap_str;
	info_str.audio_str = audio_str;
	info_str.pcm_str = pcm_str;
	info_str.ip_src = ip_src;
	info_str.ip_dst = ip_dst;
	info_str.ssrc = ssrc;
	info_str.type_num = type_num;
	info_str.a_line = a_line;
	info_str.pkt_count = pkt_count;
	info_str.max_kbps = max_kbps;
	info_str.fec = fec;
	info_str.ptime = ptime;
	
	parse_method_type = 2;
	parse_exit = 0;
	if(parse_method_type != 1){
		info_str.env = env;
	}
	gs_class=(*env)->NewGlobalRef(env,objcetClass);
	decode(&info_str);
	clearPCMBuffer(&pcm_buffers);
	if(parse_exit == 0){
		LOGI("nativeStartPlayback completed !");
		(*env)->CallStaticVoidMethod(env, objcetClass, methodID_setProgressRateFull);
	}
	else{
		LOGI("nativeStartPlayback Cancel !!!");
		(*env)->CallStaticVoidMethod(env, objcetClass, methodID_setProgressRateEmpty);
	}
	if(gs_class!=NULL)
		(*env)->DeleteGlobalRef(env,gs_class);
    return 0;
}


JNIEXPORT void JNICALL Java_com_example_pcapdecoder_activity_MainActivity_getPayloadCancel(JNIEnv *env, jobject thiz)
{
	parse_method_type = 2;
    parse_exit = 1;
}

jint JNI_OnLoad(JavaVM* vm, void* reserved){
	gs_jvm = vm;
	LOGI("JNI_OnLoad");
	return JNI_VERSION_1_4;
}
