#include "roboteq_controller/roboteq_controller_node.h"

// static const std::string tag {"[RoboteQ] "};
static const std::string tag {""};

void RoboteqDriver::declare(){
	declare_parameter<std::string>("serial_port", "dev/ttyACM0");
	declare_parameter("baudrate", 115200);
	declare_parameter("closed_loop", true);
	declare_parameter("diff_drive_mode", true);
	declare_parameter("wheel_circumference", 0.0);
	declare_parameter("track_width", 0.0);
	declare_parameter("max_rpm", 0.0);
	declare_parameter("gear_reduction", 0.0);
	declare_parameter<int>("frequency", 0);
	declare_parameter<std::string>("cmd_vel_topic", "cmd_vel");
}

void RoboteqDriver::init(){
	RCLCPP_INFO(get_logger(), "Creating");
	get_parameter("frequency", frequency_);

	get_parameter("serial_port", serial_port_);
	get_parameter("baudrate", baudrate_);

	get_parameter("closed_loop", closed_loop_);
	get_parameter("diff_drive_mode", diff_drive_mode_);
	
	get_parameter("wheel_circumference", wheel_circumference_);
	get_parameter("track_width", track_width_);
	get_parameter("max_rpm", max_rpm_);
	get_parameter("gear_reduction", gear_reduction_);
	get_parameter("cmd_vel_topic", cmd_vel_topic_);

	RCLCPP_INFO_STREAM(this->get_logger(),tag << "cmd_vel:" << cmd_vel_topic_);

	if (closed_loop_){
		RCLCPP_INFO_STREAM(this->get_logger(),tag << "In CLOSED-LOOP mode!!!! serial port:" << serial_port_);
	}
	else{
		RCLCPP_INFO_STREAM(this->get_logger(),tag << "In OPEN-LOOP mode!!!!");
	}

	if (wheel_circumference_ <=0.0 ){
		RCLCPP_ERROR_STREAM(this->get_logger(),tag << "Inproper configuration! wheel_circumference need to be greater than zero.");
	}

	if (track_width_ <=0.0 ){
		RCLCPP_ERROR_STREAM(this->get_logger(),tag << "Inproper configuration! track_width need to be greater than zero.");
	}

	if ( max_rpm_ <=0.0 ){
		RCLCPP_ERROR_STREAM(this->get_logger(),tag << "Inproper configuration! max_rpm need to be greater than zero.");
	}

	if ( gear_reduction_ <=0.0 ){
		RCLCPP_ERROR_STREAM(this->get_logger(),tag << "Inproper configuration! gear_reduction need to be greater than zero.");
	}

	if (frequency_ <= 0.0){
		RCLCPP_ERROR_STREAM(this->get_logger(),tag << "Inproper configuration! \'frequency\' need to be greater than zero.");
	}

	// Nested params for queries
	// get_parameter();

	auto param_interface = this->get_node_parameters_interface();
	std::map<std::string, rclcpp::ParameterValue> params = param_interface->get_parameter_overrides();

	// RCLCPP_INFO_STREAM(this->get_logger(), tag << "queries:" );

	for (auto iter = params.begin(); iter != params.end(); iter++){
		std::size_t pos = iter->first.find("query");
		if (pos != std::string::npos && 
			iter->second.get_type() == rclcpp::ParameterType::PARAMETER_STRING){
  			std::string topic = iter->first.substr (pos+ 6);    
			auto query = iter->second.to_value_msg().string_value;
			
			queries_[topic] =  query;
			// RCLCPP_INFO(this->get_logger(), "%15s : %s",  topic.c_str(), query.c_str() );
		}
	}
}


RoboteqDriver::RoboteqDriver(const rclcpp::NodeOptions &options): Node("roboteq_controller", options),
	serial_port_("dev/ttyUSB0"),
	baudrate_(112500),
	closed_loop_(false),
	diff_drive_mode_(false),
	wheel_circumference_(0.),
	track_width_(0.),
	max_rpm_(0.),
	gear_reduction_(0.),
	cmd_vel_topic_("cmd_vel"),
	frequency_(0){
	
	declare();
	init();

	if (diff_drive_mode_){
		cmd_vel_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
		cmd_vel_topic_,
		rclcpp::SystemDefaultsQoS(),
		std::bind(&RoboteqDriver::cmdVelCallback,
		this,
		std::placeholders::_1));
	}
	else{
		cmd_vel_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
		cmd_vel_topic_,
		rclcpp::SystemDefaultsQoS(),
		std::bind(&RoboteqDriver::powerCmdCallback,
		this,
		std::placeholders::_1));
	}

	// Initiate communication to serial port
	try{
		ser_.setPort(serial_port_);
		ser_.setBaudrate(baudrate_);
		serial::Timeout timeout = serial::Timeout::simpleTimeout(1000);
		ser_.setTimeout(timeout);
		ser_.open();
	}
	catch (serial::IOException &e){
		RCLCPP_ERROR_STREAM(this->get_logger(),tag << "Unable to open port " << serial_port_);
		rclcpp::shutdown();
	}

	if (ser_.isOpen()){
		RCLCPP_INFO_STREAM(this->get_logger(),tag << "Serial Port " << serial_port_ << " initialized");
	}
	else{
		RCLCPP_INFO_STREAM(this->get_logger(),tag << "Serial Port " << serial_port_ << " is not open");
		rclcpp::shutdown();
	}

	cmdSetup();

	run();
}


void RoboteqDriver::cmdSetup(){
    // stop motors
    std::string stop_motor_1 = "!G 1 0\r";
    std::string stop_motor_2 = "!G 2 0\r";
    std::string stop_motor_s1 = "!S 1 0\r";
    std::string stop_motor_s2 = "!S 2 0\r";
    RCLCPP_INFO(this->get_logger(), "[RoboteQ] Writing to serial: %s", stop_motor_1.c_str());
    RCLCPP_INFO(this->get_logger(), "[RoboteQ] Writing to serial: %s", stop_motor_2.c_str());
    RCLCPP_INFO(this->get_logger(), "[RoboteQ] Writing to serial: %s", stop_motor_s1.c_str());
    RCLCPP_INFO(this->get_logger(), "[RoboteQ] Writing to serial: %s", stop_motor_s2.c_str());
    ser_.write(stop_motor_1);
    ser_.write(stop_motor_2);
    ser_.write(stop_motor_s1);
    ser_.write(stop_motor_s2);
    ser_.flush();

    // enable watchdog timer (1000 ms) to stop if no connection
    std::string watchdog = "^RWD 1000\r";
    RCLCPP_INFO(this->get_logger(), "[RoboteQ] Writing to serial: %s", watchdog.c_str());
    ser_.write(watchdog);

    // set motor operating mode (1 for closed-loop speed)
    if (!closed_loop_){
        std::string open_loop_motor_1 = "^MMOD 1 0\r";
        std::string open_loop_motor_2 = "^MMOD 2 0\r";
        RCLCPP_INFO(this->get_logger(), "[RoboteQ] Writing to serial: %s", open_loop_motor_1.c_str());
        RCLCPP_INFO(this->get_logger(), "[RoboteQ] Writing to serial: %s", open_loop_motor_2.c_str());
        ser_.write(open_loop_motor_1);
        ser_.write(open_loop_motor_2);
    } else {
        std::string closed_loop_motor_1 = "^MMOD 1 1\r";
        std::string closed_loop_motor_2 = "^MMOD 2 1\r";
        RCLCPP_INFO(this->get_logger(), "[RoboteQ] Writing to serial: %s", closed_loop_motor_1.c_str());
        RCLCPP_INFO(this->get_logger(), "[RoboteQ] Writing to serial: %s", closed_loop_motor_2.c_str());
        ser_.write(closed_loop_motor_1);
        ser_.write(closed_loop_motor_2);
    }
    ser_.flush();
}



void RoboteqDriver::run(){
	initializeServices();
	std::stringstream ss0, ss1;
	ss0 << "^echof 1_";
	ss1 << "# c_/\"DH?\",\"?\"";
	for (auto item : queries_){
		RCLCPP_INFO_STREAM(this->get_logger(),tag << "Publish topic: " << item.first);
		query_pub_.push_back(create_publisher<roboteq_interfaces::msg::ChannelValues>(item.first, 100));

		std::string cmd = item.second;
		ss1 << cmd << "_";
	}
	ss1 << "# " << frequency_ << "_";
	ser_.write(ss0.str());
	ser_.write(ss1.str());
	ser_.flush();
	
    serial_read_pub_ = create_publisher<std_msgs::msg::String>("read", rclcpp::SystemDefaultsQoS());
	std::chrono::duration<int, std::milli> dt (1000/frequency_);
	timer_pub_ = create_wall_timer(dt, std::bind(&RoboteqDriver::queryCallback, this) );
}


void RoboteqDriver::powerCmdCallback(const geometry_msgs::msg::Twist::SharedPtr msg){
    std::stringstream cmd_str;
    if (closed_loop_){
        cmd_str << "!G 1"
                << " " << msg->linear.x << "_"
                << "!G 2"
                << " " << msg->angular.z << "_";
    }
    else{
        cmd_str << "!G 1"
                << " " << msg->linear.x << "_"
                << "!G 2"
                << " " << msg->angular.z << "_";
    }

    std::string cmd = cmd_str.str();
    RCLCPP_INFO(this->get_logger(), "[RoboteQ] Writing to serial: %s", cmd.c_str());
    ser_.write(cmd);
    ser_.flush();
    RCLCPP_INFO(this->get_logger(), "[ROBOTEQ] left: %9.3f right: %9.3f", msg->linear.x, msg->angular.z);
}

void RoboteqDriver::cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg) {
    std::stringstream cmd_str;

    // Calculate right and left speed based on linear and angular velocities
    float right_speed = msg->linear.x + track_width_ * msg->angular.z / 2.0;
    float left_speed  = msg->linear.x - track_width_ * msg->angular.z / 2.0;
    // Check if both speeds are zero to stop the motor
    if (msg->linear.x == 0.0 && msg->angular.z == 0.0) {
        // Stop both motors
        std::string stop_cmd = "!G 1 0\r!G 2 0\r";
        RCLCPP_INFO(this->get_logger(), "[RoboteQ] Stopping motors");
        ser_.write(stop_cmd);
        ser_.flush();
        return;
    }
    if (!closed_loop_) {
        // Calculate motor power in open-loop (scale 0-1000)
        float right_power = right_speed * 500.0 * 60.0 / (wheel_circumference_ * max_rpm_);
        float left_power  = left_speed * 80.0 * 60.0 / (wheel_circumference_ * max_rpm_);

        RCLCPP_INFO(this->get_logger(), "[ROBOTEQ] left: %9d right: %9d", (int)left_power, (int)right_power);

        // For linear velocity (move forward/backward), command motor 1 (G1) and stop motor 2 (G2)
        if (msg->linear.x != 0) {
            cmd_str << "!G 1" << " " << (int)left_power << "_";  // Command for motor 1 (linear velocity)
            cmd_str << "!G 2" << " 0\r";  // Stop motor 2 (angular velocity set to 0)
        }
        // For angular velocity (turning), command motor 2 (G2) and stop motor 1 (G1)
        else if (msg->angular.z != 0) {
            cmd_str << "!G 1" << " 0\r";  // Stop motor 1 (linear velocity set to 0)
            cmd_str << "!G 2" << " " << (int)right_power << "_";  // Command for motor 2 (angular velocity)
        }
    } else {
        // Calculate motor RPM in closed-loop (rpm)
        int32_t right_rpm = - gear_reduction_ * right_speed * 60.0 / wheel_circumference_;
        int32_t left_rpm  = gear_reduction_ * left_speed * 60.0 / wheel_circumference_;

        RCLCPP_INFO(this->get_logger(), "[ROBOTEQ] left: %9d right: %9d", left_rpm, right_rpm);

        // For linear velocity (move forward/backward), command motor 1 (G1) and stop motor 2 (G2)
        if (msg->linear.x != 0) {
            cmd_str << "!G 1" << " " << left_rpm << "_";  // Command for motor 1 (linear velocity)
            cmd_str << "!G 2" << " 0\r";  // Stop motor 2 (angular velocity set to 0)
        }
        // For angular velocity (turning), command motor 2 (G2) and stop motor 1 (G1)
        else if (msg->angular.z != 0) {
            cmd_str << "!G 1" << " 0\r";  // Stop motor 1 (linear velocity set to 0)
            cmd_str << "!G 2" << " " << right_rpm << "_";  // Command for motor 2 (angular velocity)
        }
    }

    // Send the constructed command string to the serial port
    ser_.write(cmd_str.str());
    ser_.flush();
}



void RoboteqDriver::configService(const std::shared_ptr<roboteq_interfaces::srv::Config::Request> request, std::shared_ptr<roboteq_interfaces::srv::Config::Response> response){
	std::stringstream str;
	str << "^" << request->user_input << " " << request->channel << " " << request->value << "_ " << "%%clsav321654987";
	ser_.write(str.str());
	ser_.flush();
	response->result = str.str();
	RCLCPP_INFO_STREAM(this->get_logger(),tag << response->result);
}

void RoboteqDriver::commandService(const std::shared_ptr<roboteq_interfaces::srv::Command::Request> request, std::shared_ptr<roboteq_interfaces::srv::Command::Response> response){
	std::stringstream str;
	str << "!" << request->user_input << " " << request->channel << " " << request->value << "_";
	ser_.write(str.str());
	ser_.flush();
	response->result = str.str();
	RCLCPP_INFO_STREAM(this->get_logger(),tag << response->result);
}

void RoboteqDriver::maintenanceService(const std::shared_ptr<roboteq_interfaces::srv::Maintenance::Request> request, std::shared_ptr<roboteq_interfaces::srv::Maintenance::Response> response){
	std::stringstream str;
	str << "%" << request->user_input << " " << "_";
	ser_.write(str.str());
	ser_.flush();
	response->result = ser_.read(ser_.available());
	RCLCPP_INFO_STREAM(this->get_logger(),response->result);
}


void RoboteqDriver::initializeServices(){
	config_service_server_ = create_service<roboteq_interfaces::srv::Config>(
        "config_service", std::bind(&RoboteqDriver::configService, this, std::placeholders::_1, std::placeholders::_2));

	command_service_server_ = create_service<roboteq_interfaces::srv::Command>(
        "command_service", std::bind(&RoboteqDriver::commandService, this, std::placeholders::_1, std::placeholders::_2));

	maintenance_service_server_ = create_service<roboteq_interfaces::srv::Maintenance>(
        "maintenance_service", std::bind(&RoboteqDriver::maintenanceService, this, std::placeholders::_1, std::placeholders::_2));
}


void RoboteqDriver::queryCallback(){
    auto current_time = this->now();
    if (ser_.available()) {
        std_msgs::msg::String result;
        
        // Lock to prevent concurrent access to serial communication
        std::lock_guard<std::mutex> lock(locker);
        
        // Read available data from the serial port
        result.data = ser_.read(ser_.available());

        // Check if result.data is not empty
        if (result.data.empty()) {
            RCLCPP_WARN_STREAM(this->get_logger(), tag << "No data received from serial port.");
            return; // Skip processing if no data is received
        }

        // Publish the raw serial data for debugging
        serial_read_pub_->publish(result);

        // Clean the received data
        boost::replace_all(result.data, "\r", "");
        boost::replace_all(result.data, "+", "");

        // Split the data by the delimiter "D"
        std::vector<std::string> fields;
        boost::split(fields, result.data, boost::algorithm::is_any_of("D"));

        // Check if the fields vector has at least 2 elements
        if (fields.size() < 2) {
            RCLCPP_ERROR_STREAM(this->get_logger(), tag << "Empty data:{" << result.data << "}");
        } else {
            std::vector<std::string> fields_H;
            for (int i = fields.size() - 1; i >= 0; i--) {
                if (fields[i][0] == 'H') {
                    try {
                        fields_H.clear();
                        boost::split(fields_H, fields[i], boost::algorithm::is_any_of("?"));
                        if (fields_H.size() >= query_pub_.size() + 1) {
                            break;
                        }
                    } catch (const std::exception &e) {
                        RCLCPP_ERROR_STREAM(this->get_logger(), tag << "Error parsing query output: " << fields[i]);
                        continue;
                    }
                }
            }

            if (!fields_H.empty() && fields_H[0] == "H") {
                for (long unsigned int i = 0; i < fields_H.size() - 1; ++i) {
                    std::vector<std::string> sub_fields_H;
                    boost::split(sub_fields_H, fields_H[i + 1], boost::algorithm::is_any_of(":"));
                    
                    roboteq_interfaces::msg::ChannelValues msg;
                    msg.header.stamp = current_time;

                    // Convert sub_fields_H to integers and publish the message
                    for (long unsigned int j = 0; j < sub_fields_H.size(); j++) {
                        try {
                            msg.value.push_back(boost::lexical_cast<int>(sub_fields_H[j]));
                        } catch (const std::exception &e) {
                            RCLCPP_ERROR_STREAM(this->get_logger(), tag << "Invalid data on Serial: " << result.data);
                            RCLCPP_ERROR_STREAM(this->get_logger(), e.what());
                        }
                    }
                    query_pub_[i]->publish(msg);
                }
            }
        }
    }
}



int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<RoboteqDriver>());
  rclcpp::shutdown();
  return 0;
}
