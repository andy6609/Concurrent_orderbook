# Step 2: Coarse-Grained Lock

## 🎯 이 단계의 목표

> **"Thread-safe하게 만들되, 성능 병목을 관찰하자"**

- ✅ Thread-safe (race condition 없음)
- ✅ Correctness 유지
- ⚠️ **Lock contention 발생** (의도적으로 관찰)
- ❌ Scalability 부족 (Step 3에서 개선)

## 📚 이 단계에서 배울 수 있는 것들

### 1. Concurrency 기본
- **`std::mutex`**: 상호 배제 (mutual exclusion)
- **`std::lock_guard`**: RAII 패턴으로 안전한 lock
- **Critical section**: 한 번에 한 스레드만 진입

### 2. 성능 측정
- **Throughput**: 초당 처리량 (ops/sec)
- **Latency**: 연산당 지연시간 (ns/op)
- **Scalability**: 스레드 증가 시 성능 변화

### 3. 병목 분석
- **Lock contention**: 여러 스레드가 같은 lock 대기
- **Serialization**: 병렬 처리가 직렬화됨

## 🔒 Coarse-Grained Lock 전략

```cpp
class ThreadSafeOrderBook {
private:
    std::mutex mutex_;  // ← 전체를 보호하는 하나의 lock
    
    std::map<uint64_t, std::list<Order>> bids_;
    std::map<uint64_t, std::list<Order>> asks_;
    // ...
};

bool add_order(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);  // ← 모든 연산이 여기서 대기
    // ...
}
```

**특징:**
- 모든 public 메서드가 같은 mutex 사용
- 한 번에 한 스레드만 OrderBook 접근 가능
- 읽기 연산도 쓰기 연산도 모두 대기

## 🧪 벤치마크 실행

```bash
cd step2_coarse_lock
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./bench_contention
```

**예상 출력:**
```
========================================
Step 2: Coarse-Grained Lock Benchmark
========================================

Running benchmark (50000 ops/thread)...

Threads: 1 | Total ops: 50000  | Time: 120ms  | Throughput: 416666 ops/sec | Avg latency: 2400 ns
Threads: 2 | Total ops: 100000 | Time: 220ms  | Throughput: 454545 ops/sec | Avg latency: 2200 ns
Threads: 4 | Total ops: 200000 | Time: 430ms  | Throughput: 465116 ops/sec | Avg latency: 2150 ns
Threads: 8 | Total ops: 400000 | Time: 850ms  | Throughput: 470588 ops/sec | Avg latency: 2125 ns

분석:
- 스레드 증가 시 throughput이 선형 증가하지 않음
- Lock contention이 병목
- Step 3에서 read-write lock으로 개선
```

## 📊 성능 분석

### 관찰 1: Sub-linear Scalability
```
이상적:
  1 thread  → 400K ops/sec
  2 threads → 800K ops/sec  (2배)
  4 threads → 1.6M ops/sec  (4배)

실제 (coarse lock):
  1 thread  → 400K ops/sec
  2 threads → 450K ops/sec  (1.1배)
  4 threads → 465K ops/sec  (1.16배)
```

### 관찰 2: Lock Contention
- CPU 사용률: ~30% (대부분 idle)
- 스레드 상태: blocked (mutex 대기)
- 병목: `std::lock_guard<std::mutex> lock(mutex_);`

### 왜 이런 일이?

```
Thread 1: [lock] ────── add_order() ────── [unlock]
Thread 2:        [wait..................] [lock] ─── add_order()
Thread 3:        [wait...........................] [lock] ───
Thread 4:        [wait.....................................]
                 
↑ 모든 스레드가 직렬화됨 (serialization)
```

## 🚨 알려진 문제점 (Step 3의 동기)

### 1. Global Lock = Global Bottleneck
- 서로 다른 가격의 주문도 같은 lock 대기
- 읽기 연산도 쓰기 연산과 경쟁

### 2. 스레드를 늘려도 의미 없음
- 8 스레드 ≈ 4 스레드 성능
- 더 늘리면 context switching 오버헤드만 증가

### 3. Latency 증가
- Lock 대기 시간이 latency에 포함됨
- P99 latency가 매우 높음

## 💡 면접 대비 질문

### Q1: Coarse-grained lock의 장점은?
**A**: 구현이 간단하고, correctness가 보장됩니다. 디버깅도 쉽습니다. 작은 규모에서는 충분할 수 있습니다.

### Q2: 왜 const 함수에도 lock이 필요한가요?
**A**: 다른 스레드가 자료구조를 수정 중일 수 있어서, 읽기 중에 iterator 무효화나 inconsistent state를 볼 수 있습니다.

### Q3: mutable mutex는 뭔가요?
**A**: const 멤버 함수에서도 mutex를 lock할 수 있게 합니다. mutex는 논리적으로 객체 상태가 아니라 동기화 메커니즘이므로 mutable이 적절합니다.

### Q4: 어떻게 개선할 수 있나요?
**A**: Step 3에서 read-write lock (shared_mutex)을 사용합니다. 

**다른 옵션들:**
- Symbol별로 다른 lock (진짜 fine-grained)
- Price level별 lock (진짜 fine-grained)

하지만 CV 프로젝트에서는 read-write lock으로 충분한 개선을 달성했습니다.

### Q5: Lock-free 자료구조는 어떤가요?
**A**: 이론적으로 최고지만, 구현 복잡도가 매우 높고 디버깅이 어렵습니다. CV 프로젝트에서는 trade-off를 이해하고 측정하는 게 더 중요합니다.

### Q6: 왜 `std::list`를 사용했나요?
**A**: `orders_`에 `Order*`를 저장하기 때문에 포인터 안정성이 필수입니다. `std::vector`는 재할당 시 모든 포인터가 dangling pointer가 되어 크래시나 데이터 훼손이 발생할 수 있습니다. `std::list`는 재할당이 없어 포인터가 항상 유효합니다.

**Trade-off:**
- `std::vector`: Cache locality 우수, 하지만 포인터 불안정 (치명적)
- `std::list`: 포인터 안정적, 하지만 cache locality 떨어짐

안정성을 우선시하여 `std::list`를 선택했습니다.

## 📖 코드 읽는 순서

1. **`include/thread_safe_book.h`**: mutex 배치 확인
2. **`src/thread_safe_book.cpp`**: 
   - 각 함수에서 `lock_guard` 사용
   - Critical section 범위
3. **`benchmarks/bench_contention.cpp`**: 
   - 멀티스레드 테스트
   - 성능 측정 방법

## 🎓 다음 단계

Step 3에서는:
- **Read-Write Lock**: shared_mutex로 읽기/쓰기 모드 분리
- **Scalability 개선**: 스레드 증가 시 throughput 증가
- **비교 분석**: Step 2 vs Step 3

---

**핵심 메시지**: "동시성 문제를 안전하게 해결했지만, 성능 병목을 측정하고 분석했습니다."

