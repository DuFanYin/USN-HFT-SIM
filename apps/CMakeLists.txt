add_executable(order_gateway
    order_tcp/order_gateway.cpp
)
target_link_libraries(order_gateway PRIVATE usn_core pthread)

add_executable(order_client
    order_tcp/order_client.cpp
)
target_link_libraries(order_client PRIVATE usn_core pthread)

add_executable(feed_publisher
    market_udp/feed_publisher.cpp
)
target_link_libraries(feed_publisher PRIVATE usn_core pthread)

add_executable(feed_subscriber
    market_udp/feed_subscriber.cpp
)
target_link_libraries(feed_subscriber PRIVATE usn_core pthread)

