#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/sysfs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aadarsh Mandal");
MODULE_DESCRIPTION("A simple bmp280 i2c device driver");
MODULE_VERSION("0.1");

typedef s32 BMP280_S32_t;
typedef u32 BMP280_U32_t;
typedef s64 BMP280_S64_t;

typedef union{
    u8 calib_buf[24];
    struct{
        u16 dig_T1;
        s16 dig_T2;
        s16 dig_T3;
        u16 dig_P1;
        s16 dig_P2;
        s16 dig_P3;
        s16 dig_P4;
        s16 dig_P5;
        s16 dig_P6;
        s16 dig_P7;
        s16 dig_P8;
        s16 dig_P9;
    }fields;
}Bmp_280_calib;

//Need custom private data structure 
struct bmp280_data {
    struct i2c_client *client;
    Bmp_280_calib dev_calib;
    BMP280_S32_t t_fine;
    u8 buffer[6];
};

/* convert the raw temperature data to a temp value */
static BMP280_S32_t bmp280_compensate_T_int32(struct bmp280_data *data, BMP280_S32_t adc_T) {
    BMP280_S32_t var1, var2, T;

    // Fixed en-dashes (–) to standard ASCII hyphens (-)
    var1 = ((((adc_T >> 3) - ((BMP280_S32_t)data->dev_calib.fields.dig_T1 << 1))) * ((BMP280_S32_t)data->dev_calib.fields.dig_T2)) >> 11;
    
    var2 = (((((adc_T >> 4) - ((BMP280_S32_t)data->dev_calib.fields.dig_T1)) * ((adc_T >> 4) - ((BMP280_S32_t)data->dev_calib.fields.dig_T1))) >> 12) * 
            ((BMP280_S32_t)data->dev_calib.fields.dig_T3)) >> 14;
    
    data->t_fine = var1 + var2;
    
    T = (data->t_fine * 5 + 128) >> 8;
    return T;
}

/* convert the raw data to the pressure */
static BMP280_U32_t bmp280_compensate_P_int64(struct bmp280_data *data,BMP280_S32_t adc_P) {
    BMP280_S64_t var1, var2, p;

    var1 = ((BMP280_S64_t)data->t_fine) - 128000;
    var2 = var1 * var1 * (BMP280_S64_t)data->dev_calib.fields.dig_P6;
    var2 = var2 + ((var1 * (BMP280_S64_t)data->dev_calib.fields.dig_P5) << 17);
    var2 = var2 + (((BMP280_S64_t)data->dev_calib.fields.dig_P4) << 35);
    var1 = ((var1 * var1 * (BMP280_S64_t)data->dev_calib.fields.dig_P3) >> 8) + ((var1 * (BMP280_S64_t)data->dev_calib.fields.dig_P2) << 12);
    var1 = (((((BMP280_S64_t)1) << 47) + var1)) * ((BMP280_S64_t)data->dev_calib.fields.dig_P1) >> 33;

    if (var1 == 0) {
        return 0; // Avoid exception caused by division by zero
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((BMP280_S64_t)data->dev_calib.fields.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((BMP280_S64_t)data->dev_calib.fields.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((BMP280_S64_t)data->dev_calib.fields.dig_P7) << 4);

    return (BMP280_U32_t)p;
}

/**
 * This function reads the raw temperature value from the bmp280 data register.
 * */
static BMP280_S32_t get_bmp280_tempertaure(struct bmp280_data *data){
    int ret = i2c_smbus_read_i2c_block_data(data->client,0xF7,6,data->buffer);
    if (ret < 0)
    {
        pr_err("bmp280_driver: Failed to read from I2C bus! Error code: %d\n", ret);
        return ret;
    }
    return ((u32)data->buffer[3]<<12)|((u32)data->buffer[4]<<4)|((u32)data->buffer[5]>>4);
}

/**
 * This function reads the raw pressure value from the bmp280 data register.
 * */
static BMP280_S32_t get_bmp280_pressure(struct bmp280_data *data){
    int ret = i2c_smbus_read_i2c_block_data(data->client,0xF7,6,data->buffer);
    if (ret < 0)
    {
        pr_err("bmp280_driver: Failed to read from I2C bus! Error code: %d\n", ret);
        return ret;
    }
    return ((u32)data->buffer[0]<<12)|((u32)data->buffer[1]<<4)|((u32)data->buffer[2]>>4);
}

static ssize_t temperature_show(struct device *dev,struct device_attribute *attr, char* buf){
    struct bmp280_data *data = dev_get_drvdata(dev);

    BMP280_S32_t raw_temp_value = get_bmp280_tempertaure(data);

    BMP280_S32_t final_temp = bmp280_compensate_T_int32(data,raw_temp_value)/100;

    return sysfs_emit(buf,"Tempeature in C :%d\n",final_temp);
}

static ssize_t pressure_show(struct device *dev,struct device_attribute *attr, char* buf){
    struct bmp280_data *data = dev_get_drvdata(dev);

    BMP280_S32_t raw_pressure_value = get_bmp280_tempertaure(data);

    BMP280_S32_t final_press = bmp280_compensate_P_int64(data,raw_pressure_value)/25600;

    return sysfs_emit(buf,"Pressure in Pa :%d\n",final_press);
}

/**
 * Bmp280 sensor initlisation process:
 * 1. Read the chip id and verify.
 * 2. Reset the chip before configuring.
 * 3. Configure control register (0xF4).
 * 4. Configure status register (0xF5).
 * 5. Verfiying the configurations.
 * 6. Reading the calibration values.
 */
static int bmp280_hw_init(struct bmp280_data* data){
    int ret = 0; 
    struct i2c_client *client = data->client;

    /**
     * 1. reads chip_id, negative value indicates disconnected bus,
     * or error
     */
    ret = i2c_smbus_read_byte_data(client, 0xD0);
    if (ret < 0)
    {
        pr_err("bmp280_driver: Failed to read from I2C bus! Error code: %d\n", ret);
        return ret;
    }
    else if (ret != 0x58)
    {
        pr_info("bmp2780_driver: Found, but the chip id is 0x%2x is invalid\n", ret);
        return -ENODEV;
    }
    pr_info("bmp280_driver: Chip id: 0x%2x is valid\n", ret);

    /**
     * 2. Reset the chip
     */
    ret = i2c_smbus_write_byte_data(client, 0xE0, 0xB6);
    usleep_range(2000, 3000);
    if (ret < 0)
    {
        pr_err("bmp280_driver: Failed to reset from I2C bus! Error code: %d\n", ret);
        return ret;
    }
    pr_info("bmp280_driver: Reset status sucessfull");

    /**
     * configuring the control register to choose
     * the indoor navigation mode
     * 1. mode : Normal mode
     * 2. temp oversampling : 2
     * 3. pressure oversampling: 16
     * so the hex value for this register we have to enter will
     * be 0x57.
     * */
    ret = i2c_smbus_write_byte_data(client, 0xF4, 0x57);
    if (ret < 0)
    {
        pr_info("bmp280_driver: Failed to configure from I2C bus !Error code: %d\n", ret);
        return ret;
    }
    pr_info("bmp280_driver: Configured the control register sucessfully\n");

    /**
     * Selecting the standby period = 0.5ms for the normal mode, and also setting
     * the IIR filter coefficinet value=16,
     */
    ret = i2c_smbus_write_byte_data(client, 0xF5, 0x10);
    if (ret < 0)
    {
        pr_info("bmp280_driver: Failed to configure from I2C bus !Error code: %d\n", ret);
        return ret;
    }
    pr_info("bmp280_driver: Configured the standby time and iir coefficient sucessfully\n");

    pr_info("bmp280_driver: Performing the last check up before commencing the initialisation successful, by reading the configure register values\n"); 
    ret = i2c_smbus_read_byte_data(client,0xF4);
    if (ret < 0)
    {
        pr_err("bmp280_driver: Failed to read from I2C bus! Error code: %d\n", ret);
        return ret;
    }
    if(ret!=0x57){
        pr_err("bmp280_driver: control register value 0x%02x is invalid", ret);
        return -EINVAL;
    }
    pr_info("bmp280_driver:Control register read value 0x%02x is valid\n",ret); 
    
    ret = i2c_smbus_read_byte_data(client,0xF5);
    if (ret < 0)
    {
        pr_err("bmp280_driver: Failed to read from I2C bus! Error code: %d\n", ret);
        return ret;
    }
    if(ret!=0x10){
        pr_err("bmp280_driver:Config register value 0x%02x is invalid", ret);
        return -EINVAL;
    }
    pr_info("bmp280_driver:Config register read value 0x%02x is valid\n",ret); 

    /* Reading the calibration values, updating the data structure*/
    ret = i2c_smbus_read_i2c_block_data(client,0x88,sizeof(Bmp_280_calib),data->dev_calib.calib_buf);
    if (ret < 0)
    {
        pr_err("bmp280_driver: Failed to read from I2C bus! Error code: %d\n", ret);
        return ret;
    }
    return 0;
}

static int my_bmp280_probe(struct i2c_client *client)
{
    int ret;
    pr_info("Using bmp280 driver probing\n");
    struct bmp280_data* data = devm_kzalloc(&client->dev,sizeof(struct bmp280_data),GFP_KERNEL);
    if(!data)
        return -ENOMEM;

    data->client = client;

    /* stores our private data structure in device client data*/
    i2c_set_clientdata(client,data);
    
    /**
     * Initialising the bmp280 sensor module
     */
    ret = bmp280_hw_init(data);
    if (ret < 0) {
        pr_err("bmp280_driver: Hardware initialization failed: %d\n", ret);
        return ret;
    }
    BMP280_S32_t temp = get_bmp280_tempertaure(data);

    BMP280_S32_t press  = get_bmp280_pressure(data);
    
    pr_info("bmp280_driver:Press raw data read: %d\n",press);
    
    pr_info("bmp280_driver:Temp raw data read: %d\n",temp); 
    
    pr_info("bmp280_driver:curr_temp : %d curr_press : %d\n",bmp280_compensate_T_int32(data,temp),bmp280_compensate_P_int64(data,press)); 
    
    pr_info("bmp280_driver:Driver initialized sucessfully\n"); 
    return 0;
}

/* This should put the sensor in the sleep mode */
static void my_bmp280_remove(struct i2c_client *client)
{
    pr_info("Calling the bmp280 driver remove sucessful\n");

}
/**
 * The device tree match table,
 * In this table you write the comptabile string which must be match
 * against the string written in the .dts file.
 */
static const struct of_device_id bmp280_of_match[] = {
    {.compatible = "custom,my-bmp280"},
    {}};

/**
 * This part here helps with the auto loading of the driver, when the linux
 * finds hardware which matches the string with the compatible you pass in
 * the id table it knows to loads the driver for that device.
 */
MODULE_DEVICE_TABLE(of, bmp280_of_match);

static const struct i2c_device_id bmp280_i2c_id[] = {
    {"my-bmp280", 0},
    {}};
MODULE_DEVICE_TABLE(i2c, bmp280_i2c_id);

/**
 * By calling this it creates structuer of type device_attribute with 
 * respective names dev_attr_<name>.
 * where this creates a read only type, with the name of the attribute
 * in 0444 mode i.e read only type, and the <name>_show to read the value
 * and <name>_store to store the value
 * */
static DEVICE_ATTR_RO(temperature);
static DEVICE_ATTR_RO(pressure);

/* this groups/organizes the above two attributes we declared above */
static struct attribute *dev_attrs[] = {
    &dev_attr_temperature.attr,
    &dev_attr_pressure.attr,
    NULL,
};

static const struct attribute_group bmp280_attr_group = {
    .attrs = dev_attrs,
};

static const struct attribute_group *bmp280_group[]={
    &bmp280_attr_group,
    NULL,
};

static struct i2c_driver my_bmp280_driver = {
    .driver = {
        .name = "bmp280_driver",
        .of_match_table = bmp280_of_match,
        .dev_groups = bmp280_group,
    },
    .probe = my_bmp280_probe,
    .remove = my_bmp280_remove,
    .id_table = bmp280_i2c_id,
};

module_i2c_driver(my_bmp280_driver);
