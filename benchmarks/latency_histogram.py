import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import sys

def analyze_telemetry(csv_path: str):
    print(f"[METRICS] Loading telemetry data from {csv_path}...")
    
    try:
        df = pd.read_csv(csv_path)
    except FileNotFoundError:
        print(f"[ERROR] Could not find {csv_path}. Did you run the Validator and Exchange Simulator?")
        sys.exit(1)

    # =========================================================================
    # CONFIGURATION: Hardware Specifics
    # =========================================================================
    # For Apple Silicon (M1/M2/M3), the hardware timer ticks at 24 MHz.
    # For Intel/AMD, this should be your CPU Base Frequency (e.g., 2.4 GHz = 2_400_000_000).
    TICKS_PER_SECOND = 24_000_000  # Defaulting to Apple Silicon
    
    # How many initial packets to ignore while the CPU caches "warm up"
    WARMUP_PACKETS = 1000  

    if len(df) <= WARMUP_PACKETS:
        print("[ERROR] Not enough data to bypass warmup phase.")
        sys.exit(1)

    # =========================================================================
    # DATA CLEANING & CONVERSION
    # =========================================================================
    print(f"[METRICS] Dropping the first {WARMUP_PACKETS} packets for CPU cache warmup...")
    df = df.iloc[WARMUP_PACKETS:].copy()

    # Convert raw hardware cycles into pure nanoseconds
    # (Cycles / Cycles Per Second) * 1,000,000,000 = Nanoseconds
    conversion_factor = 1_000_000_000 / TICKS_PER_SECOND
    df['Latency_ns'] = df['LatencyCycles'] * conversion_factor

    # =========================================================================
    # CALCULATING THE HFT PERCENTILES
    # =========================================================================
    latencies = df['Latency_ns'].values
    
    p50 = np.percentile(latencies, 50)     # Median - Typical latency
    p90 = np.percentile(latencies, 90)     # 90% of packets are faster than this
    p99 = np.percentile(latencies, 99)     # 99% of packets are faster than this
    p99_9 = np.percentile(latencies, 99.9) # The "Tail" latency (Crucial for HFT)
    p99_99 = np.percentile(latencies, 99.99)
    max_lat = np.max(latencies)

    print("\n" + "="*50)
    print(" TICK-TO-TRADE LATENCY METRICS (NANOSECONDS)")
    print("="*50)
    print(f"Total Packets Processed : {len(latencies):,}")
    print(f"Median (p50)            : {p50:.2f} ns")
    print(f"90th Percentile (p90)   : {p90:.2f} ns")
    print(f"99th Percentile (p99)   : {p99:.2f} ns")
    print(f"99.9th Percentile       : {p99_9:.2f} ns")
    print(f"99.99th Percentile      : {p99_99:.2f} ns")
    print(f"Absolute Maximum        : {max_lat:.2f} ns")
    print("="*50 + "\n")

    # =========================================================================
    # PLOTTING THE HISTOGRAM
    # =========================================================================
    print("[METRICS] Generating latency histogram...")
    
    # To make the graph readable, we clip the absolute massive outliers 
    # (usually OS context switches that interrupt the spin-loop)
    clip_threshold = np.percentile(latencies, 99.95)
    filtered_data = latencies[latencies <= clip_threshold]

    plt.figure(figsize=(12, 6))
    
    # Plot the histogram
    plt.hist(filtered_data, bins=100, color='#1f77b4', edgecolor='black', alpha=0.7)
    
    # Add vertical lines for our key percentiles
    plt.axvline(p50, color='green', linestyle='dashed', linewidth=2, label=f'p50: {p50:.0f} ns')
    plt.axvline(p90, color='orange', linestyle='dashed', linewidth=2, label=f'p90: {p90:.0f} ns')
    plt.axvline(p99, color='red', linestyle='dashed', linewidth=2, label=f'p99: {p99:.0f} ns')

    plt.title('Tick-to-Trade Micro-Latency Distribution (Excluding Warmup)', fontsize=16, fontweight='bold')
    plt.xlabel('Latency (Nanoseconds)', fontsize=14)
    plt.ylabel('Frequency (Packet Count)', fontsize=14)
    
    # Use a logarithmic y-scale because HFT data is heavily right-skewed
    plt.yscale('log') 
    
    plt.legend(fontsize=12)
    plt.grid(True, which="both", ls="--", alpha=0.2)
    plt.tight_layout()
    
    plt.savefig('latency_histogram.png', dpi=300)
    print("[METRICS] Success! Histogram saved as 'latency_histogram.png'.")
    plt.show()

if __name__ == "__main__":
    analyze_telemetry("latency_cycles.csv")