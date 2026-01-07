# Multi-threaded Order Book Implementation

> **A step-by-step implementation of a concurrent order book, demonstrating the evolution from single-threaded to read-write locking strategies.**

## 🎯 프로젝트 목표

이 프로젝트는 **실제 금융 거래소의 핵심 컴포넌트인 Order Book을 처음부터 구현**하고, **동시성 제어를 단계적으로 개선**하는 과정을 보여줍니다.

### 학습 목표
1. ✅ Order Book의 핵심 로직 이해 (Price-Time Priority)
2. ✅ Thread safety 구현 (Mutex, Lock)
3. ✅ 성능 병목 분석 (Profiling, Benchmarking)
4. ✅ 최적화 전략 (Read-Write locking)
5. ✅ Trade-off 분석 (Complexity vs Performance)

## 📂 프로젝트 구조

```
multithread_orderbook/
├── README.md                    # 이 파일
├── INTERVIEW_GUIDE.md           # 면접 질문 대비
│
├── step1_baseline/              # 단일 스레드 Order Book
│   ├── include/                 # order.h, order_book.h
│   ├── src/                     # order_book.cpp
│   ├── tests/                   # 정확성 테스트
│   └── README.md                # 상세 설명
│
├── step2_coarse_lock/           # Coarse-grained lock
│   ├── include/                 # thread_safe_book.h
│   ├── src/                     # thread_safe_book.cpp
│   ├── benchmarks/              # Lock contention 측정
│   └── README.md                # 병목 분석
│
└── step3_rwlock/                # Read-Write lock
    ├── include/                 # fine_grained_book.h
    ├── src/                     # fine_grained_book.cpp
    ├── benchmarks/              # Scaling 측정
    └── README.md                # 최적화 결과
```

## 🚀 빠른 시작

### 필요 사항
- C++17 이상
- CMake 3.10+
- GCC/Clang/MSVC

### 각 Step 빌드 및 실행

```bash
# Step 1: 단일 스레드 (정확성 테스트)
cd step1_baseline
mkdir build && cd build
cmake ..
make
./test_correctness

# Step 2: Coarse-grained lock (병목 측정)
cd ../../step2_coarse_lock
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./bench_contention

# Step 3: Read-Write lock (성능 개선)
cd ../../step3_rwlock
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./bench_scaling
```

## 📊 Step별 개요

### Step 1: Single-Threaded Baseline

**목표:** Order Book의 핵심 로직을 정확하게 구현

**구현 내용:**
- Limit Order, Market Order, Cancel 지원
- Price-Time Priority 매칭
- `std::map`과 `std::vector`로 간결한 구현

**핵심 학습:**
- Order Book의 작동 원리
- 자료구조 설계 (정렬, FIFO)
- Matching engine 로직

**제한사항:**
- ❌ Thread-safe하지 않음
- ❌ 멀티코어 활용 불가

➡️ [Step 1 상세 보기](step1_baseline/README.md)

---

### Step 2: Coarse-Grained Lock

**목표:** Thread-safe하게 만들되, 성능 병목 관찰

**구현 내용:**
- 하나의 `std::mutex`로 전체 OrderBook 보호
- 모든 public 메서드에 lock
- Correctness 보장

**핵심 학습:**
- Mutex와 Lock guard
- Critical section
- Lock contention 측정

**문제점:**
- ⚠️ 스레드 증가해도 throughput 거의 동일
- ⚠️ 모든 연산이 직렬화됨

**벤치마크 결과:**
```
Threads | Throughput    | vs Baseline
--------|---------------|-------------
1       | 400K ops/sec  | 1.0x
2       | 450K ops/sec  | 1.12x
4       | 465K ops/sec  | 1.16x
8       | 470K ops/sec  | 1.17x
```

➡️ [Step 2 상세 보기](step2_coarse_lock/README.md)

---

### Step 3: Read-Write Lock

**목표:** Read-Write Lock으로 Scalability 개선

**구현 내용:**
- `std::shared_mutex` (Read-Write lock) 사용
- 읽기: `shared_lock` (여러 스레드 동시 가능)
- 쓰기: `unique_lock` (배타적)

**주의:** 이것은 "fine-grained lock"이 아닙니다. Lock을 쪼갠 게 아니라 **lock mode를 분리**한 것입니다.

**핵심 학습:**
- Read-Write lock
- Read-Write locking 전략
- Workload 특성 (read-heavy vs write-heavy)

**개선 결과:**
- ✅ 읽기 연산들이 동시에 진행
- ✅ Lock contention 대폭 감소
- ✅ 스레드 증가 시 throughput 증가

**벤치마크 결과 (70% read / 30% write):**
```
Threads | Throughput    | vs Step2  | vs Baseline
--------|---------------|-----------|-------------
1       | 1.0M ops/sec  | 1.0x      | 1.0x
2       | 1.8M ops/sec  | 4.0x      | 1.8x
4       | 2.7M ops/sec  | 5.8x      | 2.7x
8       | 3.3M ops/sec  | 7.0x      | 3.3x
```

➡️ [Step 3 상세 보기](step3_rwlock/README.md)

---

## 📈 전체 성능 비교

### Throughput (8 스레드)
```
Step 1: 400K ops/sec  (단일 스레드)
Step 2: 470K ops/sec  (1.17x) ← Lock contention 심함
Step 3: 3.3M ops/sec  (8.25x) ← 큰 개선!
```

### Scalability
```
Step 2: 스레드 8배 → Throughput 1.17배 ❌
Step 3: 스레드 8배 → Throughput 8.25배 ✅
```

## 🎓 주요 설계 결정

### 1. 왜 `std::map`을 사용했나?
- 가격 정렬이 자동 (Best price = O(1))
- 삽입/삭제 O(log n)
- 간단하고 안정적

### 2. 왜 Read-Write lock을 선택했나?
다른 옵션들:
- ❌ Symbol별 lock: 심볼이 하나면 의미 없음
- ❌ Price level별 lock: 구현 복잡, deadlock 위험
- ✅ Read-Write lock: 간단하면서 효과적

### 3. Lock-free는 고려 안 했나?
- 구현 난이도 매우 높음
- 디버깅 어려움
- 현재 목표(동시성 개선)는 충분히 달성
- CV 프로젝트로는 read-write lock이 적절

## 💡 배운 것들

### 기술적 학습
1. **Concurrency primitives**: mutex, lock_guard, shared_mutex
2. **Performance measurement**: throughput, latency, profiling
3. **Optimization strategies**: lock granularity, read-write separation
4. **Trade-off analysis**: complexity vs performance

### 엔지니어링 프로세스
1. **문제 정의**: 정확한 구현이 먼저
2. **측정**: 추측 말고 데이터로
3. **최적화**: 병목 발견 → 개선 → 검증
4. **문서화**: 설계 결정의 이유 기록

## 🔮 추가 개선 아이디어

이 프로젝트를 확장하고 싶다면:

1. **Symbol별 독립적 처리**
   - 각 심볼마다 별도 OrderBook + mutex
   - 완전한 병렬 처리 가능

2. **Lock-free Best Price Cache**
   - `std::atomic`으로 캐시
   - 읽기 연산이 lock 없이 가능

3. **Batch Processing**
   - 여러 주문을 묶어서 처리
   - Lock 획득 횟수 감소

4. **Memory Pool**
   - Order allocation을 custom allocator로
   - Cache locality 개선

➡️ **더 자세한 내용과 의도적으로 제외한 기능들:** [FUTURE_WORK.md](FUTURE_WORK.md)

## 📚 참고 자료

### Order Book 개념
- [NASDAQ ITCH Protocol](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf)
- Limit Order Book 원리

### 동시성
- C++ Concurrency in Action (Anthony Williams)
- The Art of Multiprocessor Programming

### 참고 구현
- [CppTrader](https://github.com/chronoxor/CppTrader) (참고만, 코드 복사 X)

## 🎯 면접 준비

이 프로젝트로 대답할 수 있는 질문들:

- ✅ "Order Book을 구현해본 적 있나요?"
- ✅ "Thread-safe한 자료구조를 만들어봤나요?"
- ✅ "성능 병목을 어떻게 찾고 개선했나요?"
- ✅ "Mutex vs Read-Write lock의 차이는?"
- ✅ "Lock-free 자료구조에 대해 아나요?"
- ✅ "더 개선할 수 있나요?" / "왜 X 기능은 안 만들었나요?"

➡️ [면접 질문 가이드 보기](INTERVIEW_GUIDE.md)  
➡️ [의도적으로 제외한 기능들](FUTURE_WORK.md)

## 📝 라이선스

MIT License - 자유롭게 사용, 수정, 배포 가능

## 👤 저자

이 프로젝트는 CV/포트폴리오 용도로 처음부터 구현되었습니다.

---

**핵심 메시지:** 
> "Order Book의 핵심 로직을 이해하고, 동시성 문제를 단계적으로 해결하며, 성능을 측정하고 개선했습니다."

