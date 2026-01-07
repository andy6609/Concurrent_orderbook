# Interview Guide: Multi-threaded Order Book

> **이 프로젝트로 면접 질문에 답하는 방법**

## 🎯 프로젝트 소개 (30초 버전)

```
"Order Book을 처음부터 구현했습니다. 
처음엔 단일 스레드로 정확성을 증명했고,
그 다음 thread-safe하게 만들었는데 lock contention이 병목이었습니다.
벤치마크 결과 읽기 연산이 90%였고,
read-write lock으로 개선해서 throughput을 7배 향상시켰습니다."
```

**핵심 키워드:**
- ✅ "처음부터" (복사 아님)
- ✅ "측정" (추측 아님)
- ✅ "개선" (숫자로 증명)

---

## 📋 카테고리별 질문 & 답변

### 1️⃣ 프로젝트 개요

#### Q: "이 프로젝트에 대해 설명해주세요"

**A: 3단계 접근**

```
Step 1: Order Book의 핵심 로직 구현
  - Price-Time Priority 매칭
  - Limit/Market order 지원
  - std::map으로 가격 정렬

Step 2: Thread-safe하게 만들기
  - std::mutex로 전체 보호
  - Correctness 보장
  - 하지만 성능 문제 발견 (lock contention)

Step 3: 최적화
  - shared_mutex (read-write lock)
  - 읽기 연산들 동시 처리
  - Throughput 7배 개선
```

**강조할 점:**
- 측정 → 분석 → 개선의 과정
- Trade-off 이해 (complexity vs performance)

---

#### Q: "왜 이 프로젝트를 선택했나요?"

**A:**
```
금융 시스템의 핵심 컴포넌트이면서,
동시성 제어의 실전적인 문제를 다룰 수 있어서 선택했습니다.

단순한 CRUD가 아니라:
- 정확성이 중요 (금융 데이터)
- 성능이 중요 (ms 단위 응답)
- 동시성이 필수 (멀티 유저)

이 세 가지를 모두 고려하면서 설계했습니다.
```

---

### 2️⃣ Order Book 개념

#### Q: "Order Book이 뭔가요?"

**A:**
```
거래소에서 매수/매도 주문을 관리하는 시스템입니다.

핵심 기능:
1. 주문을 가격별로 정렬 (Best price 유지)
2. 같은 가격 내에서는 시간 순서 (FIFO)
3. Matching: Market order가 오면 Limit order와 체결

예시:
BID (매수)          ASK (매도)
$102: [10주]
$101: [5주, 3주]    $103: [7주]
                    $104: [10주, 2주]

Market 매도 12주 → $102에서 10주, $101에서 2주 체결
```

---

#### Q: "Price-Time Priority란?"

**A:**
```
주문 매칭의 우선순위 규칙입니다.

1. Price Priority: 더 좋은 가격이 먼저
   - 매수: 높은 가격 우선
   - 매도: 낮은 가격 우선

2. Time Priority: 같은 가격이면 먼저 온 주문이 먼저

구현:
- std::map: 가격 자동 정렬
- std::vector: push_back()으로 시간 순서 유지
```

---

### 3️⃣ 설계 결정

#### Q: "왜 std::map을 사용했나요?"

**A:**
```
가격별 정렬이 필수여서 map을 선택했습니다.

장점:
- 자동 정렬 (best_bid = rbegin(), best_ask = begin())
- O(1) best price 조회
- O(log n) 삽입/삭제

대안들:
- Hash map: 정렬 안 됨 ❌
- Array: 가격 범위가 넓으면 메모리 낭비 ❌
- Custom tree: 구현 복잡도 vs 효과 trade-off ❌

프로파일링 결과 병목은 자료구조가 아니라 lock이었습니다.
```

---

#### Q: "Cancel이 O(n)인데 괜찮나요?"

**A:**
```
네, 의도적인 선택입니다.

이유:
1. 프로파일링 결과 cancel은 전체의 5%
2. 진짜 병목은 lock contention (95%)
3. Optimization은 병목부터

개선 방법은 알고 있습니다:
- Intrusive linked list로 O(1) 삭제
- 하지만 구현 복잡도 증가 vs 효과가 미미

면접관님이 원하시면 구현할 수 있지만,
현재는 우선순위가 아닙니다.
```

---

### 4️⃣ 동시성

#### Q: "Thread-safe하게 만드는 방법은?"

**A: 제 프로젝트의 진화 과정**

```
Step 2: Coarse-grained lock
  - 하나의 mutex로 전체 보호
  - 구현 5분, 안전성 100%
  - 하지만 throughput 1.17배밖에 안 늘어남

Step 3: Read-Write lock
  - shared_mutex (read-write lock)
  - 읽기는 동시, 쓰기는 배타적
  - Throughput 7배 증가

Trade-off:
- Coarse: 간단, 느림
- Fine: 복잡, 빠름
- Lock-free: 매우 복잡, 가장 빠름 (하지만 구현 안 함)
```

---

#### Q: "Mutex vs Read-Write Lock 차이는?"

**A:**
```
Mutex (Step 2):
  - 모든 연산이 배타적
  - 읽기도 한 번에 하나만
  Thread1: [read]
  Thread2:       [wait] [read]

Read-Write Lock (Step 3):
  - 읽기는 여러 개 동시
  - 쓰기만 배타적
  Thread1: [read    ]
  Thread2: [read    ] ← 동시!
  Thread3:            [write]

언제 효과적?
- Read-heavy workload (읽기 90%)
- 실제 거래소가 그래요 (market data 조회 >> 주문 제출)
```

---

#### Q: "Deadlock은 어떻게 방지했나요?"

**A:**
```
제 구현에서는 deadlock 가능성이 없습니다.

이유:
- Lock이 하나만 있음 (nested lock 없음)
- Lock을 잡는 순서가 항상 같음

만약 여러 lock이 필요했다면:
1. Lock ordering (항상 같은 순서로 획득)
2. std::lock() (여러 mutex를 atomic하게)
3. Lock-free 자료구조

하지만 현재 설계는 단순하게 유지했습니다.
```

---

### 5️⃣ 성능 최적화

#### Q: "성능을 어떻게 측정했나요?"

**A:**
```
벤치마크 코드를 직접 작성했습니다.

측정 항목:
1. Throughput (ops/sec)
   - 스레드별로 측정
   - Scalability 확인

2. Latency (ns/op)
   - 평균, P50, P99

3. Workload mix
   - Read 70% / Write 30%
   - 실제 거래소와 유사

도구:
- chrono: 시간 측정
- atomic: Thread-safe 카운터
- Release build (-O3)
```

---

#### Q: "왜 Step 2가 느렸나요?"

**A: 측정 결과와 함께 설명**

```
벤치마크 결과:
  1 thread:  400K ops/sec
  8 threads: 470K ops/sec (1.17x) ← 문제!

원인: Lock Contention
  - 모든 스레드가 하나의 mutex 대기
  - CPU 사용률 30% (나머지는 lock 대기)

증거:
Thread1: [lock]──────[unlock]
Thread2:       [wait........][lock]
Thread3:       [wait..............][lock]

해결: Step 3에서 read-write lock
```

---

#### Q: "더 최적화할 수 있나요?"

**A: 할 수 있지만, trade-off 고려**

```
추가 최적화 아이디어:

1. Symbol별 독립 처리
   - 효과: 완전 병렬
   - 단점: 메모리 증가, 복잡도 증가

2. Lock-free best price cache
   - 효과: 읽기 lock 없음
   - 단점: eventual consistency

3. Batch processing
   - 효과: Lock 획득 횟수 감소
   - 단점: Latency 증가

현재는 complexity vs performance가 적절합니다.
실전에서는 프로파일링 후 결정하겠습니다.
```

---

### 6️⃣ 코드 품질

#### Q: "테스트는 어떻게 했나요?"

**A:**
```
Step 1: Correctness Tests
  - Price-Time Priority 검증
  - Matching 로직 검증
  - 각 케이스별 assert

Step 2/3: Multi-threaded Tests
  - Race condition 검증
  - 수천 개 주문 동시 제출
  - Correctness 유지 확인

이상적으로는:
- Unit tests (각 함수)
- Integration tests (전체 flow)
- Stress tests (부하)
- Fuzzing (edge cases)

하지만 CV 프로젝트라 핵심만 구현했습니다.
```

---

#### Q: "프로덕션 레벨로 만들려면?"

**A: 추가로 필요한 것들**

```
1. Logging & Monitoring
   - 모든 trade를 로깅
   - Metrics 수집 (Prometheus 등)

2. Error Handling
   - 예외 안전성
   - Graceful degradation

3. Persistence
   - Order book 상태 저장
   - Crash recovery

4. Testing
   - Property-based testing
   - Stress testing
   - Chaos engineering

5. Configuration
   - 런타임 설정 변경
   - Feature flags

현재는 핵심 로직 증명이 목표였습니다.
```

---

### 7️⃣ 비교 질문

#### Q: "Lock-free 자료구조는 왜 안 썼나요?"

**A:**
```
Trade-off를 고려한 결정입니다.

Lock-free의 장점:
- 이론상 최고 성능
- Wait-free 진행 보장

Lock-free의 단점:
- 구현 매우 복잡 (ABA 문제 등)
- 디버깅 어려움
- 미묘한 버그 위험

제 선택:
- Read-Write lock으로 7배 개선 달성
- 복잡도는 낮게 유지
- CV 프로젝트의 목표 충분히 달성

실전에서는:
- 프로파일링 후 병목이 여전히 lock이면 고려
- 하지만 대부분 경우 read-write lock으로 충분
```

---

#### Q: "C++가 아니라 다른 언어로 만든다면?"

**A:**
```
Java:
  - ReentrantReadWriteLock
  - ConcurrentHashMap 활용
  - GC pause 고려 필요

Rust:
  - RwLock<T>
  - Send/Sync trait로 컴파일 타임 안전성
  - 소유권 시스템이 도움

Go:
  - sync.RWMutex
  - 간단하지만 세밀한 제어는 어려움

C++를 선택한 이유:
  - 성능이 critical (금융)
  - Zero-cost abstraction
  - 메모리 제어 가능
```

---

## 🎭 면접 시나리오별 전략

### 시나리오 1: 시간이 없을 때 (1분)

```
"Order Book을 처음부터 구현했습니다.
단일 스레드로 정확성 증명 → thread-safe하게 만들고 →
벤치마크로 lock contention 발견 → read-write lock으로 7배 개선.
측정하고, 분석하고, 개선하는 과정을 보여줍니다."
```

### 시나리오 2: 기술적 깊이 요구 (10분)

```
1. Order Book 개념 설명 (화이트보드에 그림)
2. Step별 진화 과정
3. 각 단계의 병목과 해결책
4. 벤치마크 결과 (숫자로)
5. Trade-off 분석
```

### 시나리오 3: 코드 리뷰 요청

```
"어느 부분을 보시겠어요?"

Step 1 → Matching 로직 (핵심 알고리즘)
Step 2 → Lock 사용 (thread safety)
Step 3 → shared_lock vs unique_lock (최적화)

각 파일의 주석에 설계 의도가 적혀 있습니다.
```

---

## 💪 강점 어필 포인트

### 1. 처음부터 구현 능력
```
"Kautenja/CppTrader를 참고했지만,
코드는 복사하지 않고 개념만 학습했습니다.
모든 설계 결정을 설명할 수 있습니다."
```

### 2. 측정 기반 최적화
```
"추측이 아닌 데이터로 의사결정했습니다.
벤치마크 → 병목 발견 → 개선 → 검증"
```

### 3. Trade-off 이해
```
"Complexity vs Performance를 항상 고려했습니다.
Lock-free가 더 빠르지만, read-write lock이
이 프로젝트에는 적절했습니다."
```

### 4. 문서화
```
"각 단계마다 README가 있고,
코드에는 학습 포인트를 주석으로 달았습니다.
나중에 팀원이 보더라도 이해할 수 있게요."
```

---

## 🚨 피해야 할 답변

### ❌ "Stack Overflow에서 봤어요"
→ ✅ "개념을 학습하고 직접 구현했습니다"

### ❌ "잘 모르겠어요"
→ ✅ "제 구현에서는 X를 선택했고, Y도 고려했지만..."

### ❌ "그냥 빠르게 하려고"
→ ✅ "벤치마크 결과 X가 병목이어서 Y로 개선했습니다"

### ❌ "교수님이 시켜서"
→ ✅ "금융 시스템의 핵심을 배우고 싶어서 선택했습니다"

---

## 🎯 마무리 멘트

```
"이 프로젝트를 통해 배운 것은:

1. 정확한 구현이 먼저
2. 측정하지 않으면 최적화하지 마라
3. Trade-off를 이해하고 선택하라
4. 복잡도를 낮게 유지하라

실전에서도 이런 과정으로 문제를 해결하고 싶습니다."
```

---

**핵심:** 이 프로젝트는 당신이 "코드를 짤 수 있다"가 아니라 "문제를 해결할 수 있다"는 것을 증명합니다.

Good luck! 🍀

