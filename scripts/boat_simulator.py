#!/usr/bin/env python3
"""
Boat Simulator - Reads XYZ file and broadcasts NMEA0183 RMC and DPT sentences over UDP.
The boat follows a serpentine pattern through the grid (left-to-right, then right-to-left).
"""

import argparse
import csv
import math
import socket
import time
from datetime import datetime, timezone
from collections import defaultdict


def calculate_nmea_checksum(sentence):
    """Calculate NMEA0183 checksum (XOR of all characters between $ and *)."""
    checksum = 0
    for char in sentence:
        checksum ^= ord(char)
    return f"{checksum:02X}"


def decimal_to_nmea_lat(decimal_deg):
    """Convert decimal degrees latitude to NMEA format (DDMM.MMMM)."""
    direction = 'N' if decimal_deg >= 0 else 'S'
    decimal_deg = abs(decimal_deg)
    degrees = int(decimal_deg)
    minutes = (decimal_deg - degrees) * 60
    return f"{degrees:02d}{minutes:07.4f}", direction


def decimal_to_nmea_lon(decimal_deg):
    """Convert decimal degrees longitude to NMEA format (DDDMM.MMMM)."""
    direction = 'E' if decimal_deg >= 0 else 'W'
    decimal_deg = abs(decimal_deg)
    degrees = int(decimal_deg)
    minutes = (decimal_deg - degrees) * 60
    return f"{degrees:03d}{minutes:07.4f}", direction


def calculate_course(lat1, lon1, lat2, lon2):
    """Calculate course (bearing) between two points in degrees."""
    lat1_rad = math.radians(lat1)
    lat2_rad = math.radians(lat2)
    delta_lon = math.radians(lon2 - lon1)

    x = math.sin(delta_lon) * math.cos(lat2_rad)
    y = math.cos(lat1_rad) * math.sin(lat2_rad) - math.sin(lat1_rad) * math.cos(lat2_rad) * math.cos(delta_lon)

    bearing = math.degrees(math.atan2(x, y))
    return (bearing + 360) % 360


def calculate_distance_nm(lat1, lon1, lat2, lon2):
    """Calculate distance between two points in nautical miles using Haversine formula."""
    R = 3440.065  # Earth's radius in nautical miles

    lat1_rad = math.radians(lat1)
    lat2_rad = math.radians(lat2)
    delta_lat = math.radians(lat2 - lat1)
    delta_lon = math.radians(lon2 - lon1)

    a = math.sin(delta_lat / 2) ** 2 + math.cos(lat1_rad) * math.cos(lat2_rad) * math.sin(delta_lon / 2) ** 2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))

    return R * c


def generate_rmc(lat, lon, speed_knots, course, dt=None):
    """Generate NMEA RMC sentence."""
    if dt is None:
        dt = datetime.now(timezone.utc)

    time_str = dt.strftime("%H%M%S.00")
    date_str = dt.strftime("%d%m%y")

    lat_nmea, lat_dir = decimal_to_nmea_lat(lat)
    lon_nmea, lon_dir = decimal_to_nmea_lon(lon)

    # RMC format: $GPRMC,time,status,lat,N/S,lon,E/W,speed,course,date,magvar,E/W,mode*checksum
    sentence_body = f"GPRMC,{time_str},A,{lat_nmea},{lat_dir},{lon_nmea},{lon_dir},{speed_knots:.1f},{course:.1f},{date_str},,"
    checksum = calculate_nmea_checksum(sentence_body)
    return f"${sentence_body}*{checksum}"


def generate_dpt(depth, offset=0.0):
    """Generate NMEA DPT sentence."""
    # DPT format: $SDDPT,depth,offset*checksum
    # Depth should be positive (below transducer)
    depth_positive = abs(depth)
    sentence_body = f"SDDPT,{depth_positive:.1f},{offset:.1f}"
    checksum = calculate_nmea_checksum(sentence_body)
    return f"${sentence_body}*{checksum}"


def load_xyz_file(filepath):
    """Load XYZ file and return points organized in serpentine pattern."""
    rows = defaultdict(list)

    with open(filepath, 'r') as f:
        reader = csv.reader(f)
        header = next(reader)  # Skip header

        for row in reader:
            if len(row) >= 3:
                lon = float(row[0].strip())
                lat = float(row[1].strip())
                depth = float(row[2].strip())
                rows[lat].append((lon, lat, depth))

    # Sort rows by latitude (descending to go from top to bottom)
    sorted_lats = sorted(rows.keys(), reverse=True)

    # Create serpentine path
    path = []
    for i, lat in enumerate(sorted_lats):
        row_points = sorted(rows[lat], key=lambda p: p[0])  # Sort by longitude
        if i % 2 == 1:  # Reverse every other row
            row_points = row_points[::-1]
        path.extend(row_points)

    return path


def run_simulator(xyz_file, udp_host, udp_port, speed_knots, interval, batch_size):
    """Run the boat simulator."""
    print(f"Loading XYZ file: {xyz_file}")
    path = load_xyz_file(xyz_file)
    print(f"Loaded {len(path)} waypoints")

    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"Broadcasting NMEA sentences to {udp_host}:{udp_port}")
    print(f"Boat speed: {speed_knots} knots, Update interval: {interval}s, Batch size: {batch_size}")
    print("Press Ctrl+C to stop\n")

    try:
        for i, (lon, lat, depth) in enumerate(path):
            # Calculate course to next point (or use previous course for last point)
            if i < len(path) - 1:
                next_lon, next_lat, _ = path[i + 1]
                course = calculate_course(lat, lon, next_lat, next_lon)

            # Generate NMEA sentences
            rmc = generate_rmc(lat, lon, speed_knots, course)
            dpt = generate_dpt(depth)

            # Send via UDP
            sock.sendto((rmc + '\r\n').encode(), (udp_host, udp_port))
            sock.sendto((dpt + '\r\n').encode(), (udp_host, udp_port))

            # Print status only for last point in each batch
            if (i + 1) % batch_size == 0 or i == len(path) - 1:
                print(f"[{i+1}/{len(path)}] Lat: {lat:.6f}, Lon: {lon:.6f}, Depth: {abs(depth):.1f}m, Course: {course:.1f}")
                time.sleep(interval)

    except KeyboardInterrupt:
        print("\nSimulation stopped by user")
    finally:
        sock.close()

    print("Simulation complete")


def main():
    parser = argparse.ArgumentParser(description='Boat simulator - broadcasts NMEA0183 over UDP')
    parser.add_argument('xyz_file', help='Path to XYZ file')
    parser.add_argument('--host', default='127.0.0.1', help='UDP host (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=10110, help='UDP port (default: 10110)')
    parser.add_argument('--speed', type=float, default=5.0, help='Boat speed in knots (default: 5.0)')
    parser.add_argument('--interval', type=float, default=1.0, help='Update interval in seconds (default: 1.0)')
    parser.add_argument('--batch', type=int, default=1, help='Number of points to send per interval (default: 1)')
    parser.add_argument('--loop', action='store_true', help='Loop continuously through the path')

    args = parser.parse_args()

    if args.loop:
        while True:
            run_simulator(args.xyz_file, args.host, args.port, args.speed, args.interval, args.batch)
    else:
        run_simulator(args.xyz_file, args.host, args.port, args.speed, args.interval, args.batch)


if __name__ == '__main__':
    main()
