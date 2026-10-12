#include <boost/program_options.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <rclcpp/time.hpp>
#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <sensor_msgs/point_cloud2_iterator.hpp>


namespace po = boost::program_options;


constexpr char OPT_BAG[] = "bag";
constexpr char OPT_TOPIC[] = "topic";
constexpr char OPT_HELP[] = "help";


int main(int argc, char** argv) {
    const po::variables_map vm = [] (const int argc, char** argv) {
        po::variables_map vm;

        po::options_description arguments("Arguments");
        arguments.add_options()
            (OPT_BAG, po::value<std::string>(), "rosbag2 directory or storage file")
            (OPT_TOPIC, po::value<std::string>(), "topic name")
        ;

        po::positional_options_description arguments_positions;
        arguments_positions.add(OPT_BAG, 1);
        arguments_positions.add(OPT_TOPIC, 1);

        po::options_description general("General options");
        general.add_options()
            (OPT_HELP, "print this message")
        ;

        po::options_description all_options("All options");
        all_options.add(arguments).add(general);

        po::store(
            po::command_line_parser(argc, argv).options(all_options).positional(arguments_positions).run(),
            vm
        );
        po::notify(vm);

        if (
                vm.count(OPT_HELP) != 0 ||
                vm.count(OPT_BAG) != 1 ||
                vm.count(OPT_TOPIC) != 1
        ) {
            std::cout << "extract_pc2 [options]";
            std::cout << " " << OPT_BAG;
            std::cout << " " << OPT_TOPIC;
            std::cout << std::endl << std::endl;
            std::cout << arguments << std::endl;
            std::cout << general << std::endl;
            exit(1);
        }

        return vm;
    } (argc, argv);

    rosbag2_cpp::Reader bag;
    bag.open(vm[OPT_BAG].as<std::string>());
    rosbag2_storage::StorageFilter view;
    view.topics = {vm[OPT_TOPIC].as<std::string>()};
    bag.set_filter(view);
    std::string topic_type;
    for (const auto& topic : bag.get_all_topics_and_types()) {
        if (topic.name == vm[OPT_TOPIC].as<std::string>())
            topic_type = topic.type;
    }
    assert(topic_type == rosidl_generator_traits::name<sensor_msgs::msg::PointCloud2>());

    std::cout << "recieve\ttimestamp";
    std::cout << "\tx\ty";
    std::cout << "\txq\tyq\tzq\twq";
    std::cout << "\tdist";
    std::cout << std::endl;

    while (bag.has_next()) {
        const auto instance = bag.read_next();
        const builtin_interfaces::msg::Time receive_time = rclcpp::Time(instance->recv_timestamp);
        std::shared_ptr<sensor_msgs::msg::PointCloud2> msg;
        if (topic_type == rosidl_generator_traits::name<sensor_msgs::msg::PointCloud2>()) {
            msg = std::make_shared<sensor_msgs::msg::PointCloud2>();
            rclcpp::SerializedMessage serialized(*instance->serialized_data);
            rclcpp::Serialization<sensor_msgs::msg::PointCloud2> serialization;
            serialization.deserialize_message(&serialized, msg.get());
        }
        if (msg == NULL)
            continue;
        
        sensor_msgs::PointCloud2ConstIterator<double> x_iter(*msg, "x");
        sensor_msgs::PointCloud2ConstIterator<double> y_iter(*msg, "y");
        sensor_msgs::PointCloud2ConstIterator<double> xq_iter(*msg, "xq");
        sensor_msgs::PointCloud2ConstIterator<double> yq_iter(*msg, "yq");
        sensor_msgs::PointCloud2ConstIterator<double> zq_iter(*msg, "zq");
        sensor_msgs::PointCloud2ConstIterator<double> wq_iter(*msg, "wq");
        sensor_msgs::PointCloud2ConstIterator<double> dist_iter(*msg, "dist");

        std::ostringstream recv_sec, recv_nsec, stamp_sec, stamp_nsec;
        recv_sec << std::setw(9) << std::setfill('0') << receive_time.sec;
        recv_nsec << std::setw(9) << std::setfill('0') << receive_time.nanosec;
        stamp_sec << std::setw(9) << std::setfill('0') << msg->header.stamp.sec;
        stamp_nsec << std::setw(9) << std::setfill('0') << msg->header.stamp.nanosec;

        while (x_iter != x_iter.end()) {
            std::cout << recv_sec.str() << recv_nsec.str();
            std::cout << '\t' << stamp_sec.str() << stamp_nsec.str();
            std::cout << '\t' << *x_iter;
            std::cout << '\t' << *y_iter;
            std::cout << '\t' << *xq_iter;
            std::cout << '\t' << *yq_iter;
            std::cout << '\t' << *zq_iter;
            std::cout << '\t' << *wq_iter;
            std::cout << '\t' << *dist_iter;
            std::cout << std::endl;

            ++x_iter;
            ++y_iter;
            ++xq_iter;
            ++yq_iter;
            ++zq_iter;
            ++wq_iter;
            ++dist_iter;
        }
    }

    return 0;
}