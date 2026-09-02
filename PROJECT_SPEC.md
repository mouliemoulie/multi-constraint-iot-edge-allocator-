# Original Project Specification

This is the original project description provided at project kickoff.
Source-code comments throughout this repository reference it as
`spec §N` where N is the numbered section below.

---

Absolutely. Let's define the project as a realistic C++ capstone, without AI/ML, where the main focus is adaptive resource allocation and load balancing in a simulated IoT edge environment.

# Adaptive Multi-Constraint Resource Allocation and Load Balancing for Edge-Based IoT Networks

## 1. Project idea in simple terms
The project simulates a network containing many IoT devices such as ultrasonic sensors, temperature sensors, cameras, etc., and several edge computing nodes. IoT devices continuously generate data. That data becomes computational tasks that need to be processed by an edge node. The problem is that all edge nodes do not have the same available resources at every moment.

## 2. What problem are we solving?
100 IoT Devices -> Task Generation -> 10 Edge Nodes. A simple round-robin approach may still send tasks to overloaded nodes even when others are idle. This project makes allocation adaptive.

## 3. What exactly does "multi-constraint" mean?
Instead of looking at only CPU availability, the allocator considers CPU, RAM, bandwidth, latency, queue length, and deadline together, computing an overall suitability score.

## 4-14. Architecture
Full architecture: IoT Devices -> MQTT -> MQTT Broker -> Task Manager -> Resource Controller -> {Resource Monitor, Resource Allocator} -> Best Edge -> Task Scheduler -> Task Executor -> Metrics Collector -> SQLite.

## 15. Algorithms
Multi-Constraint scoring formula:
Score = Wcpu*CPU_score + Wmemory*Memory_score + Wbandwidth*BW_score
      + Wlatency*Latency_score + Wqueue*Queue_score + Wpriority*Priority_score
Compared against Round Robin, Least Load, and Priority Based baselines.

## 16-19. Simulation scale, metrics, and experimental comparison
100 devices, 10 edges, 10,000+ tasks. Metrics: average latency, waiting
time, deadline miss rate, CPU utilization, load imbalance, throughput,
task success rate. Four algorithms compared side-by-side on identical
workloads.

## 20. What makes this a good C++ capstone?
OOP (inheritance/polymorphism/encapsulation/abstraction), STL
(vector/map/queue/priority_queue), algorithms (scoring, load balancing,
scheduling, reallocation), systems concepts (resource management,
concurrency, networking, fault handling), external technologies (MQTT,
SQLite).

(Full original text with worked numeric examples for each section was
provided at kickoff and is preserved in the project's issue tracker /
original chat history.)
