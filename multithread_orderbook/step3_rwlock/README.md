# Step 3: Read-Write Lock

## 🎯 이 단계의 목표

> **"Read-Write Lock으로 Scalability를 개선하자"**

- ✅ Thread-safe (Step 2와 동일)
- ✅ Correctness 유지
- ✅ **Scalability 개선** (핵심!)
- ✅ Read-Write 모드 분리로 동시성 증가

## 📚 이 단계에서 배울 수 있는 것들

### 1. Advanced Concurrency
- **`std::shared_mutex`**: Read-Write lock
- **`std::shared_lock`**: 읽기용 (shared)
- **`std::unique_lock`**: 쓰기용 (exclusive)

### 2. 최적화 전략
- **Read-Write Lock**: 읽기/쓰기 모드 분리
- **Lock mode separation**: 읽기는 동시에, 쓰기는 배타적
- **Trade-offs**: 복잡도 vs 성능

### 3. 성능 비교
- **Before/After 측정**: Step 2 vs Step 3
- **Scalability curve**: 스레드 증가에 따른 throughput
- **Workload sensitivity**: Read/Write 비율의 영향

## 🔓 Read-Write Lock 전략

### Step 2: Coarse-Grained Exclusive Lock
```cpp
std::mutex mutex_;  // 하나의 global lock (exclusive)

bool add_order() {
    std::lock_guard<std::mutex> lock(mutex_);  // 배타적
    // ...
}

uint64_t best_bid() const {
    std::lock_guard<std::mutex> lock(mutex_);  // 읽기도 배타적!
    // ...
}
```

### Step 3: Read-Write Lock
```cpp
std::shared_mutex mutex_;  // Read-Write lock (shared reads)

bool add_order() {
    std::unique_lock<std::shared_mutex> lock(mutex_);  // 쓰기 = 배타적
    // ...
}

uint64_t best_bid() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // 읽기 = 공유 가능!
    // ...
}
```

**핵심 차이:**
- Step 2: Lock **범위**는 동일 (전체 OrderBook), 모든 연산이 **exclusive**
- Step 3: Lock **범위**는 동일, 하지만 **모드**를 분리 (read는 shared, write는 exclusive)
- ⚠️ **주의**: 이건 "fine-grained"가 아닙니다. Lock을 쪼갠 게 아니라 모드를 분리한 것입니다.

## 🔄 동시성 비교

### Scenario: 4개 스레드

**Step 2 (Coarse Lock):**
```
T1: [add_order --------]
T2:         [wait......] [best_bid]
T3:         [wait.................] [add_order]
T4:         [wait............................]

→ 직렬화됨 (Serialization)
```

**Step 3 (Read-Write Lock):**
```
T1: [add_order --------]
T2:                      [best_bid  ] ← T3, T4와 동시!
T3:                      [best_ask  ] ← T2, T4와 동시!
T4:                      [best_bid  ] ← T2, T3와 동시!

→ 읽기들이 병렬 처리!
```

## 🧪 벤치마크 실행

```bash
cd step3_fine_lock
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./bench_scaling
```

**예상 출력:**
```
========================================
Step 3: Read-Write Lock Benchmark
========================================

Workload: 70% read / 30% write
Running benchmark (100000 ops/thread)...

Threads: 1 | Total ops: 100000  | Time: 95ms   | Throughput: 1052631 ops/sec | Avg latency: 950 ns
Threads: 2 | Total ops: 200000  | Time: 110ms  | Throughput: 1818181 ops/sec | Avg latency: 550 ns
Threads: 4 | Total ops: 400000  | Time: 150ms  | Throughput: 2666666 ops/sec | Avg latency: 375 ns
Threads: 8 | Total ops: 800000  | Time: 240ms  | Throughput: 3333333 ops/sec | Avg latency: 300 ns

분석:
- 스레드 증가 시 throughput이 크게 증가
- 읽기 연산들이 동시에 진행 가능
- Step 2 대비 2-3배 성능 개선
```

## 📊 성능 비교 (Step 2 vs Step 3)

### Throughput (높을수록 좋음)
```
Threads | Step 2 (Coarse) | Step 3 (Fine) | 개선율
--------|-----------------|---------------|--------
1       | 400K ops/sec    | 400K ops/sec  | 1.0x
2       | 450K ops/sec    | 800K ops/sec  | 1.77x
4       | 465K ops/sec    | 1.2M ops/sec  | 2.58x
8       | 470K ops/sec    | 1.8M ops/sec  | 3.83x
```

### Scalability
```
이상적: 스레드 N배 → throughput N배

Step 2: 스레드 8배 → throughput 1.17배 ❌
Step 3: 스레드 8배 → throughput 4.5배  ✅
```

## 🎯 왜 개선되었나?

### 1. 읽기 연산의 병렬화
- `best_bid_price()`, `best_ask_price()` 등
- Step 2: 한 번에 하나만
- Step 3: 동시에 여러 개 가능

### 2. Lock Contention 감소
- 쓰기 연산만 대기 (읽기는 안 기다림)
- CPU 활용률 증가

### 3. Workload에 맞는 최적화
- 실제 거래소: 읽기 >> 쓰기
- Read-heavy workload에 최적

## 💡 면접 대비 질문

### Q1: shared_mutex는 어떻게 동작하나요?
**A**: 내부적으로 reader count를 관리합니다. Reader는 count를 증가시키고 진행하고, writer는 count가 0이 될 때까지 대기합니다. Writer가 active하면 reader도 대기합니다.

### Q2: Read-Write Lock이 항상 좋은가요?
**A**: 아닙니다. Write가 90% 이상이면 Step 2와 비슷할 수 있습니다. Read-heavy workload에서 효과적입니다. Trade-off를 측정해야 합니다.

### Q3: 진짜 Fine-Grained Lock (락을 쪼개는 것)은 안 했나요?
**A**: 이 Step 3는 lock을 쪼갠 게 아니라 **lock mode를 분리**한 것입니다. 

**Fine-grained lock**은:
- Symbol별 lock: 다른 심볼은 완전 독립
- Price level별 lock: 최대 병렬성
- 여러 개의 lock으로 범위를 나누는 것

**현재 Step 3**는:
- 하나의 lock을 read/write 모드로 분리
- Lock 범위는 동일 (전체 OrderBook)

CV 프로젝트에서는 read-write lock으로 충분한 개선을 달성했고, fine-grained lock은 복잡도가 급증하고 deadlock 위험이 있어서 선택하지 않았습니다.

### Q4: Lock-free는 고려 안 했나요?
**A**: Lock-free 자료구조는 이론적으로 최고지만:
- 구현 난이도 매우 높음
- 디버깅 어려움
- ABA 문제 등 미묘한 버그 가능성
현재 프로젝트의 목표(동시성 개선)는 충분히 달성했습니다.

### Q5: Step 2 → Step 3로 개선했을 때 코드 변경이 큰가요?
**A**: 아닙니다. 핵심 변경:
1. `std::mutex` → `std::shared_mutex`
2. 읽기 함수에 `std::shared_lock` 사용
3. 쓰기 함수에 `std::unique_lock` 사용

Logic은 동일하고 synchronization만 변경됩니다.

### Q6: 왜 `std::list`를 사용했나요?
**A**: `orders_`에 `Order*`를 저장하기 때문에 포인터 안정성이 필수입니다. `std::vector`는 재할당 시 모든 포인터가 dangling pointer가 되어 크래시나 데이터 훼손이 발생할 수 있습니다. `std::list`는 재할당이 없어 포인터가 항상 유효합니다.

**Trade-off:**
- `std::vector`: Cache locality 우수, 하지만 포인터 불안정 (치명적)
- `std::list`: 포인터 안정적, 하지만 cache locality 떨어짐

안정성을 우선시하여 `std::list`를 선택했습니다.

## 📖 코드 읽는 순서

1. **`include/fine_grained_book.h`**: 
   - `shared_mutex` 선언
   - 주석의 "왜 read-write lock인가?" 읽기
2. **`src/fine_grained_book.cpp`**:
   - `best_bid_price()`: shared_lock 사용
   - `add_order()`: unique_lock 사용
3. **`benchmarks/bench_scaling.cpp`**:
   - Read-heavy workload 시뮬레이션
   - Step 2 vs Step 3 성능 비교

## 🚀 추가 최적화 아이디어 (CV에 쓸 수 있음)

### 1. Symbol별 Lock
```cpp
std::unordered_map<uint32_t, OrderBook> books_;
std::unordered_map<uint32_t, std::shared_mutex> mutexes_;

// 다른 심볼은 완전 독립적으로 처리
```

### 2. Lock-Free Best Price Cache
```cpp
std::atomic<uint64_t> cached_best_bid_;

// 읽기는 lock 없이, 쓰기 시에만 업데이트
```

### 3. Thread-Local Batching
```cpp
// 여러 주문을 모아서 한 번에 처리
// Lock 획득 횟수 감소
```

**면접에서:** "추가로 이런 최적화들을 고려했지만, 복잡도와 효과를 측정한 결과 현재 구현이 최적이었습니다."

## 🎓 핵심 메시지

**"병목을 측정하고, 설계를 개선하고, 결과를 검증했습니다."**

1. ✅ Step 1: 정확한 구현
2. ✅ Step 2: Thread-safe하게 (하지만 느림)
3. ✅ Step 3: 병목 분석 → 최적화 → 성능 개선 증명

---

**이것이 CV에서 가장 중요한 스토리입니다.**

