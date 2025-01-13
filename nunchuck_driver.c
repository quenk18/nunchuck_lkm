#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/gpio/consumer.h>

#define DRIVER_NAME "nunchuck_driver"
#define BASE_MINOR 0
#define DRIVER_NUNCHUCK_MINOR 1

// function prototypes
static ssize_t nunchuck_read(struct file*, char* __user, size_t, loff_t*);
static ssize_t nunchuck_write(struct file*, const char* __user, size_t, loff_t*);
static int nunchuck_open(struct inode*, struct file*);
static int nunchuck_release(struct inode*, struct file*);

static void poll_fn(struct input_dev *dev);

static int nunchuck_probe(struct i2c_client *client);
static void nunchuck_remove(struct i2c_client *client);

struct nunchuck_driver_data {
    struct cdev cdev;
    int major;
    struct class *cls;
    struct i2c_client *client;
    struct gpio_desc *gpio_data;
    struct input_dev *input_dev;
};

struct input_driver_data {
    struct i2c_client *client;
};

static struct i2c_device_id nunchuck_tableid[] = {
    {"nunchuck", 1},
    {   } // NULL to indicate end
};
MODULE_DEVICE_TABLE(i2c, nunchuck_tableid);

static struct i2c_driver nunchuck_driver = {
    .probe = nunchuck_probe,
    .remove = nunchuck_remove,
    .driver = {
        .name = "nunchuck_spym_driver",
	.owner = THIS_MODULE
    },
    .id_table = nunchuck_tableid
};

static struct file_operations fops = {
    .read = nunchuck_read,
    .write = nunchuck_write,
    .open = nunchuck_open,
    .release = nunchuck_release
};

// functions definitions

void poll_fn(struct input_dev *dev) {
    struct input_driver_data *input_data;
    input_data = input_get_drvdata(dev);
    if (input_data == NULL || input_data->client == NULL) {
        pr_info("Input data or i2c_client is NULL!\n");
	return;
    }
    struct i2c_client *client = input_data->client;

    u8 dummy[2] = {0x40, 0x00};
    if (i2c_master_send(client, dummy, 2) != 2) {
        pr_alert("Failed to send i2c command\n");
	return;
    }
    u8 dummy_2 = 0x00;
    msleep(1);
    if (i2c_master_send(client, &dummy_2, 1) != 1) {
        pr_alert("Failed to send i2c command 2\n");
	return;
    }
    msleep(1);
    u8 rx_buf[6];
    if (i2c_master_recv(client, rx_buf, 6) != 6) {
        pr_alert("Failed to send i2c command 2\n");
	return;
    }
    u8 zpressed = (rx_buf[5] >> 0) & 1;
    u8 cpressed = (rx_buf[5] >> 1) & 1;
    input_report_key(dev, BTN_Z, zpressed);
    input_report_key(dev, BTN_C, cpressed);
    input_sync(dev);

    u8 pos_x = rx_buf[0];
    input_report_abs(dev, ABS_X, pos_x);

    u8 pos_y = rx_buf[1];
    input_report_abs(dev, ABS_Y, pos_y);

    pr_info("reg[0]: %x\n", rx_buf[5]);
}

int nunchuck_probe(struct i2c_client *client) {
    pr_info("Probing!\n");
    struct nunchuck_driver_data *data;
    data = (struct nunchuck_driver_data*)devm_kzalloc(&client->dev, sizeof(struct nunchuck_driver_data), GFP_KERNEL);
    if (!data) {
	return -EINVAL;
    }
    dev_t dev;

#if 0
    // get gpio
    struct gpio_desc *gpio_data = devm_gpiod_get(&client->dev, "GPIO22", GPIOD_OUT_HIGH);

    if (IS_ERR(gpio_data)) {
        pr_alert("Failed to get gpio handle!\n");
	return -ENODEV;
    }
    data->gpio_data = gpio_data;
#endif

    if (alloc_chrdev_region(&dev, BASE_MINOR, 1, DRIVER_NAME) != 0) {
	pr_alert("Failed to allocate chrdev region!");
	return -EFAULT;
    }
    data->major = MAJOR(dev);

    cdev_init(&data->cdev, &fops);
    cdev_add(&data->cdev, dev, DRIVER_NUNCHUCK_MINOR);

    data->cls = class_create(DRIVER_NAME);
    device_create(data->cls, NULL, dev, NULL, "nunchuck");
        
    data->client = client;
    i2c_set_clientdata(client, data);

    struct input_dev *input;
    input = devm_input_allocate_device(&client->dev);
    if (!input) {
	pr_alert("Cannot allocate device!\n");
        return -ENOMEM;
    }

    struct input_driver_data *input_data;
    input_data = (struct input_driver_data*)devm_kzalloc(&client->dev, sizeof(struct input_driver_data), GFP_KERNEL);

    input->name = "Wii Nunchuk";
    input->id.bustype = BUS_I2C;
    set_bit(EV_KEY, input->evbit);
    set_bit(EV_ABS, input->evbit);
    set_bit(BTN_C, input->keybit);
    set_bit(BTN_Z, input->keybit);
    set_bit(ABS_X, input->absbit);
    set_bit(ABS_Y, input->absbit);
    input_set_abs_params(input, ABS_X, 30, 220, 4, 8);
    input_set_abs_params(input, ABS_Y, 40, 200, 4, 8);

    if (input_setup_polling(input, poll_fn) != 0) {
        pr_alert("Failed to setup polling function for input driver!");
    }

    input_data->client = client;
    input_set_drvdata(input, input_data);

    input_set_poll_interval(input, 10);

    if (input_register_device(input) != 0) {
	pr_alert("Cannot register device!\n");
	return -EFAULT;
    }
    data->input_dev = input;

    return 0;
}

void nunchuck_remove(struct i2c_client *client) {
    struct nunchuck_driver_data *data;
    data = i2c_get_clientdata(client);
    if (!data) {
        pr_alert("Remove: Failed to get clientdata!");
    }

#if 1
    input_unregister_device(data->input_dev);
#endif

    device_destroy(data->cls, MKDEV(data->major, BASE_MINOR));
    class_destroy(data->cls);
    cdev_del(&data->cdev);
    unregister_chrdev_region(MKDEV(data->major, BASE_MINOR), 1);

#if 0
    // gpio related
    gpiod_put(data->gpio_data);
#endif

    pr_info("Removing!\n");
}

ssize_t nunchuck_read(struct file* filp, char* __user buf, size_t size, loff_t* offset) {
    pr_info("Read!\n");
    struct nunchuck_driver_data *data = filp->private_data;
    struct i2c_client *client = data->client;

    i2c_smbus_write_byte_data(client, 0x40, 0x00);
    msleep(1);
    i2c_smbus_write_byte(client, 0x00);
    msleep(1);
    u8 pos = i2c_smbus_read_byte(client);
    msleep(1);
    pr_info("Pos: %d", pos);
    return 0;
}

ssize_t nunchuck_write(struct file* filp, const char* __user buf, size_t size, loff_t* offset) {
    pr_info("Write!\n");
    return 0;
}

int nunchuck_open(struct inode* node, struct file* filp) {
    pr_info("Open!\n");
    // need to retrieve the nunchuck_driver_data
    struct nunchuck_driver_data *data;
    data = container_of(node->i_cdev, struct nunchuck_driver_data, cdev);
    if (data == NULL || data->client == NULL) {
        pr_alert("container_of\n");
	return -EFAULT;
    }
    filp->private_data = data;
    return 0;
}

int nunchuck_release(struct inode* node, struct file* filp) {
    pr_info("Release!\n");
    return 0;
}

static int __init nunchuck_driver_init(void) {
    pr_info("Init driver!\n");
    i2c_add_driver(&nunchuck_driver);
    return 0;
}

static void __exit nunchuck_driver_exit(void) {
    i2c_del_driver(&nunchuck_driver);
    pr_info("Exiting driver!\n");
}

module_init(nunchuck_driver_init);
module_exit(nunchuck_driver_exit);
MODULE_LICENSE("GPL v2");
