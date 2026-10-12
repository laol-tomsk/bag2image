#include <rosbag2_cpp/reader.hpp>
#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sensor_msgs/msg/imu.hpp>
#include <boost/program_options.hpp>


namespace po = boost::program_options;


constexpr char OPT_TOPIC[]     = "topic";
constexpr char OPT_BAG[]       = "bag";
constexpr char OPT_HELP[]      = "help";


int main(int argc, char **argv) {
    po::variables_map vm;
    {
        po::options_description pos_desc_backend("Arguments");
        pos_desc_backend.add_options()
            (OPT_BAG, po::value<std::string>(), "rosbag2 directory or storage file")
            (OPT_TOPIC, po::value<std::string>(), "topic name")
        ;

        po::positional_options_description pos_desc;
        pos_desc.add(OPT_BAG, 1);
        pos_desc.add(OPT_TOPIC, 1);

        po::options_description vis_desc("Options");
        vis_desc.add_options()
            (OPT_HELP, "print this message")
        ;

        po::options_description all_desc("All options");
        all_desc.add(vis_desc).add(pos_desc_backend);

        po::store(
            po::command_line_parser(argc, argv).options(all_desc).positional(pos_desc).run(),
            vm
        );
        po::notify(vm);

        if (
                vm.count(OPT_HELP) ||
                !vm.count(OPT_BAG) ||
                !vm.count(OPT_TOPIC)
        ) {
            std::cout << "extract_imu [options]";
            std::cout << " " << OPT_BAG;
            std::cout << " " << OPT_TOPIC;
            std::cout << std::endl << std::endl;
            std::cout << pos_desc_backend << std::endl;
            std::cout << vis_desc << std::endl;
            return 0;
        }
    }
    rosbag2_cpp::Reader bag;
    bag.open(vm[OPT_BAG].as<std::string>());
    std::vector<std::string> topics;
    const std::string topic_name = vm[OPT_TOPIC].as<std::string>();
    topics.emplace_back(topic_name);
    rosbag2_storage::StorageFilter view;
    view.topics = topics;
    bag.set_filter(view);
    std::string topic_type;
    for (const auto& topic : bag.get_all_topics_and_types()) {
        if (topic.name == topic_name)
            topic_type = topic.type;
    }
    assert(topic_type == rosidl_generator_traits::name<sensor_msgs::msg::Imu>());

    std::cout << "nsec";
    std::cout << "\tquat_x\tquat_y\tquat_z\tquat_w";
    std::cout << "\taccel_x\taccel_y\taccel_z";
    std::cout << "\tangular_x\tangular_y\tangular_z";
    std::cout << std::endl;

    while (bag.has_next()) {
        const auto instance = bag.read_next();
        std::shared_ptr<sensor_msgs::msg::Imu> msg;
        if (topic_type == rosidl_generator_traits::name<sensor_msgs::msg::Imu>()) {
            msg = std::make_shared<sensor_msgs::msg::Imu>();
            rclcpp::SerializedMessage serialized(*instance->serialized_data);
            rclcpp::Serialization<sensor_msgs::msg::Imu> serialization;
            serialization.deserialize_message(&serialized, msg.get());
        }
        if (msg == NULL)
            continue;
        
        std::ostringstream ss_sec, ss_nsec;
        ss_sec << std::setw(9) << std::setfill('0') << msg->header.stamp.sec;
        ss_nsec << std::setw(9) << std::setfill('0') << msg->header.stamp.nanosec;
        std::cout << ss_sec.str() << ss_nsec.str();
        std::cout << '\t' << msg->orientation.x;
        std::cout << '\t' << msg->orientation.y;
        std::cout << '\t' << msg->orientation.z;
        std::cout << '\t' << msg->orientation.w;
        std::cout << '\t' << msg->linear_acceleration.x;
        std::cout << '\t' << msg->linear_acceleration.y;
        std::cout << '\t' << msg->linear_acceleration.z;
        std::cout << '\t' << msg->angular_velocity.x;
        std::cout << '\t' << msg->angular_velocity.y;
        std::cout << '\t' << msg->angular_velocity.z;
        std::cout << std::endl;
    }

    bag.close();
    return 0;
}
