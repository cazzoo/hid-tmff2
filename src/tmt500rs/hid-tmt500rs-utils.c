int t500rs_send_command(struct t500rs_device_entry *t500rs, u8 cmd_type, u8 effect_id, u8 param)
{
    u8 buf[4];
    
    if (!t500rs)
        return -EINVAL;
        
    /* Build command packet according to T500RS protocol */
    buf[0] = 0x03;  /* Report ID for force feedback */
    buf[1] = cmd_type;
    buf[2] = effect_id;
    buf[3] = param;
    
    /* Send command with retries */
    return t500rs_send_buf(t500rs, buf, sizeof(buf));
} 