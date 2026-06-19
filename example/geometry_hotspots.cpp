//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#define NATIVEPG_GEOMETRY

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/describe/class.hpp>

#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>

#include "nativepg/connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "nativepg/types/geometry.hpp"

namespace asio = boost::asio;
using namespace nativepg;

struct geom_row
{
    std::string origin_name;
    types::pg_geometry<double, 2, boost::geometry::cs::geographic<boost::geometry::degree>> origin_location;
    std::string destination_name;
    types::pg_geometry<double, 2, boost::geometry::cs::geographic<boost::geometry::degree>> destination_location;
    int64_t distance_km;
};
BOOST_DESCRIBE_STRUCT(geom_row, (), (origin_name, origin_location, destination_name, destination_location, distance_km))

const std::string statement = R"sql(
WITH hotspots AS (
    SELECT 'Amsterdam' AS city, ST_SetSRID('POINT(4.895168 52.370216)'::geometry, 4326) AS location
    UNION ALL SELECT 'Rotterdam', ST_SetSRID('POINT(4.477733 51.924420)'::geometry, 4326)
    UNION ALL SELECT 'London', ST_SetSRID('POINT(-0.127758 51.507351)'::geometry, 4326)
    UNION ALL SELECT 'Newyork', ST_SetSRID('POINT(-74.005973 40.712776)'::geometry, 4326)
    UNION ALL SELECT 'Singapore', ST_SetSRID('POINT(103.851959 1.290270)'::geometry, 4326)
    UNION ALL SELECT 'Jakarta', ST_SetSRID('POINT(106.845599 -6.208763)'::geometry, 4326)
    UNION ALL SELECT 'Peking (Beijing)', ST_SetSRID('POINT(116.407396 39.904211)'::geometry, 4326)
    UNION ALL SELECT 'Hongkong', ST_SetSRID('POINT(114.162773 22.278315)'::geometry, 4326)
    UNION ALL SELECT 'Tokio', 'SRID=4326;POINT(139.6503 35.6762)':: geometry
    UNION ALL SELECT 'Calgary', 'SRID=4326;POINT(-114.0719 51.0447)'::geometry
    UNION ALL SELECT 'Toronto',  'SRID=4326;POINT(-79.3832 43.6532)'::geometry
    UNION ALL SELECT 'San Francisco', 'SRID=4326;POINT(-122.4194 37.7749)'::geometry
    UNION ALL SELECT 'Washington D.C.', 'SRID=4326;POINT(-77.0369 38.9072)'::geometry
    UNION ALL SELECT 'Seoul', 'SRID=4326;POINT(126.9780 37.5665)'::geometry
    UNION ALL SELECT 'Moscow', 'SRID=4326;POINT(37.6173 55.7558)'::geometry
    UNION ALL SELECT 'Mumbai', 'SRID=4326;POINT(72.8777 19.0760)'::geometry
    UNION ALL SELECT 'Delhi', 'SRID=4326;POINT(77.2090 28.6139)'::geometry
    UNION ALL SELECT 'Bangkok', 'SRID=4326;POINT(100.5018 13.7563)'::geometry
    UNION ALL SELECT 'Sydney', 'SRID=4326;POINT(151.2093  -33.8688)'::geometry
    UNION ALL SELECT 'Berlin', 'SRID=4326;POINT(13.4050 52.5200)'::geometry
    UNION ALL SELECT 'Paris', 'SRID=4326;POINT(2.3522 48.8566)'::geometry
    UNION ALL SELECT 'Rome', 'SRID=4326;POINT(12.4964 41.9028)'::geometry
    UNION ALL SELECT 'Prague', 'SRID=4326;POINT(14.4378 50.0755)'::geometry
    UNION ALL SELECT 'Boedapest', 'SRID=4326;POINT(19.0402 47.4979)'::geometry
    UNION ALL SELECT 'Rouveen', 'SRID=4326;POINT(6.1931 52.6226)'::geometry
)
SELECT
    a.city AS origin_name,
    a.location as origin_location,
    b.city AS destination_name,
    b.location as destination_location,
    -- Cast to geography to get meters, then divide by 1000 for kilometers
    ROUND((ST_Distance(a.location::geography, b.location::geography) / 1000)::numeric, 2)::int AS distance_km
FROM hotspots a
         CROSS JOIN hotspots b
-- Optional: Prevents matching a city to itself and eliminates duplicate pairs
WHERE a.city < b.city
ORDER BY distance_km ASC;
)sql";


// POINT to types::pg_geometry (TEXT)
static asio::awaitable<void> hotspots_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query(::statement, {});

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "Hotspots TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
    {
        std::cout << std::left;
        std::cout << "Hotspots TEXT select result: " << " (in " << duration << ")" << std::endl;

        std::cout << "| nr  |     origin city      | origin location          | destination city     | destination location     | distance in km                            |" << std::endl;
        std::cout << "|-----|----------------------|--------------------------|----------------------|--------------------------|-------------------------------------------|" << std::endl;

        auto rownr = 0;
        for (auto row: select_vec)
        {
            auto origin_location = std::get<types::pg_geometry<double, 2, boost::geometry::cs::geographic<boost::geometry::degree>>::point_t>(row.origin_location.get());
            std::stringstream ss;
            ss << boost::geometry::wkt(origin_location);
            auto origin_location_wkt = ss.str();

            auto destination_location = std::get<types::pg_geometry<double, 2, boost::geometry::cs::geographic<boost::geometry::degree>>::point_t>(row.destination_location.get());
            ss.str("");
            ss.clear();
            ss << boost::geometry::wkt(destination_location);
            auto destination_location_wkt = ss.str();

            auto bg_km = boost::geometry::distance(origin_location, destination_location) / 1000.0;
            std::cout   << "| " << std::setw(3) << ++rownr
                        << " | " << std::setw(20) << row.origin_name
                        << " | " << std::setw(24) << origin_location_wkt
                        << " | " << std::setw(20) << row.destination_name
                        << " | " << std::setw(24) << destination_location_wkt
                        << " | " << std::setw(7) << row.distance_km << "(PostGIS), " << std::setw(7) << bg_km << "(Boost.Geometry)"
                        << " | " << std::endl;
        }

        std::cout << "|-----|----------------------|--------------------------|----------------------|--------------------------|-------------------------------------------|" << std::endl;
        std::cout << "| nr  |     origin city      | origin location          | destination city     | destination location     | distance in km                            |" << std::endl;
    }
}

// POINT to types::pg_geometry (BINARY)
static asio::awaitable<void> hotspots_binary_example(connection& conn)
{
    /*
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::geometry as g", {"point_bintest"} )
        .add_execute("point_bintest", {"POINT(1.1977 2.1426)"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "POINT BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "POINT BINARY select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
        */
    co_return;
}

static asio::awaitable<void> co_main()
{
    // Create a connection
    connection conn{co_await asio::this_coro::executor};

    // Connect
    co_await conn.async_connect(
        {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"}
    );
    std::cout << "Startup complete\n";

    co_await hotspots_text_example(conn);
    co_await hotspots_binary_example(conn);

    std::cout << "Done\n";
}

int main()
{
    asio::io_context ctx;

    asio::co_spawn(ctx, co_main(), [](std::exception_ptr exc) {
        if (exc)
            std::rethrow_exception(exc);
    });

    ctx.run();
}