package com.example.pcapdecoder.bean;

/**
 * Created by h26376 on 2017/11/30.
 */

public class PktInfo implements Comparable{
    int frame;
    short seq;
    int timestamp;
    int status;

    public int getFrame() {
        return frame;
    }

    public void setSeq(short seq) {
        this.seq = seq;
    }

    public short getSeq() {
        return seq;
    }

    public int getTimestamp() {
        return timestamp;
    }

    public void setStatus(int status) {
        this.status = status;
    }

    public int getStatus() {
        return status;
    }

    @Override
    public String toString() {
        return "PktInfo{" +
                "frame=" + frame +
                ", seq=" + seq +
                ", timestamp=" + timestamp +
                ", status=" + status +
                '}';
    }

    public static int shortiToUshort(int s){
        return s&0x0FFFF;
    }

    public static long intToUint(long i){
        return i&0x0FFFFFFFFL;
    }

    @Override
    public int compareTo(Object o) {
        PktInfo pktInfo = (PktInfo)o;
        return PktInfo.shortiToUshort(this.seq) - PktInfo.shortiToUshort(pktInfo.seq);
    }
}
