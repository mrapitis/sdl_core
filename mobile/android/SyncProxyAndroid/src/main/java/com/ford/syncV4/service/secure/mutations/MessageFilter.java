package com.ford.syncV4.service.secure.mutations;

import com.ford.syncV4.proxy.RPCRequest;

/**
 * Created by Andrew Batutin on 4/14/14.
 */
public class MessageFilter {
    private static boolean encrypt;

    public static void setEncrypt(boolean encrypt) {
        MessageFilter.encrypt = encrypt;
    }

    public static boolean isEncrypt() {
        return encrypt;
    }

    public static RPCRequest filter(RPCRequest msg) {
        msg.setDoEncryption(encrypt);
        return msg;
    }
}