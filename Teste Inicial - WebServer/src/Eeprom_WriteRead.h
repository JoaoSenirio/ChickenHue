//Write a time_t variable (8 bytes) on EEPROM
void setEEPROMtime_t(time_t seconds, int position){
    int_least64_t v;
    int i; 
    v = seconds; 
    for(i = 8; i > 0; i--){
        EEPROM.write((position + (i-1)), lowByte(v));
        if(i == 1) break;
        else v = v >> 8;
    }
}

//Read and return a time_t variable (8 bytes) of EEPROM
time_t getEEPROMtime_t(int position){
    int_least64_t v = 0;
    int i; 
    for(i = 0; i < 8; i++){
        v |= EEPROM.read(position + i);
        if(i == 7) break;
        else v = v << 8; 
    }
    return v;
}