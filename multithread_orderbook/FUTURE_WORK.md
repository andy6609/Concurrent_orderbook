# Future Work & Intentionally Excluded Features

> **"무엇을 하지 않았는가도 설계 결정이다"**

이 문서는 프로젝트에서 **의도적으로 제외한 기능들**과 그 이유를 설명합니다.

---

## 📋 목차

1. [Stop/IOC/FOK 고급 주문 타입](#1-stopiocfok-고급-주문-타입)
2. [ITCH 파서](#2-itch-파서)
3. [Lock-free 자료구조](#3-lock-free-자료구조)
4. [나노초 단위 초고성능](#4-나노초-단위-초고성능-hft)
5. [CppTrader 직접 확장](#5-cpptrader-직접-확장)
6. [다중 매칭 스레드](#6-다중-매칭-스레드)
7. [추가 고려 사항](#7-추가-고려-사항)

---

## 1. Stop/IOC/FOK 고급 주문 타입

### 📖 설명

- **Stop Order**: 특정 가격(stop price) 도달 시 주문 활성화
- **IOC (Immediate-Or-Cancel)**: 즉시 체결 안 되면 취소
- **FOK (Fill-Or-Kill)**: 전량 체결 안 되면 전부 취소
- **AON (All-Or-None)**: 부분 체결 불가

### ❌ 왜 제외했나

```
핵심 문제: Order Book의 본질이 아님

1. 복잡도 증가:
   - 매칭 로직에 분기가 급증
   - 상태 관리 복잡 (Stop은 대기 → 활성화 전환)
   - 테스트 케이스 폭증

2. 프로젝트 초점 희석:
   - 동시성/락 설계와 직접적 연관 없음
   - "기능 많음" vs "핵심 정확"의 트레이드오프

3. CV 관점:
   ❌ "기능 30개 있어요" → "그래서 어떤 게 핵심인데요?"
   ✅ "핵심 3개 완벽" → "설계 사상이 명확하네요"
```

### 🔮 언제 추가할 수 있나

**조건:**
- Step 3까지 완벽히 동작
- 동시성 이슈 완전히 해결
- 추가 기능이 프로젝트 가치를 높일 때

**구현 방법:**
```cpp
// Stop Order 예시
class OrderBook {
private:
    std::map<uint64_t, std::vector<Order>> stop_bids_;  // 대기 중
    std::map<uint64_t, std::vector<Order>> stop_asks_;
    
    void check_stop_activation(uint64_t last_price) {
        // 가격 도달 시 stop → limit 전환
    }
};
```

**예상 공수:** 2-3일

### 💡 면접 답변

```
"Stop/IOC/FOK도 고려했지만 제외했습니다.
이유는 프로젝트의 핵심이 '동시성 최적화'였고,
이런 주문 타입들은 매칭 로직만 복잡하게 만들 뿐
동시성 문제와는 직접 연관이 없었습니다.

만약 추가한다면 Stop order를 먼저 구현할 것 같습니다.
실전에서 가장 많이 쓰이고, 설계도 깔끔하니까요."
```

---

## 2. ITCH 파서

### 📖 설명

- NASDAQ가 제공하는 **바이너리 시장 데이터 포맷**
- 실제 거래 내역을 재생 (replay)
- 주문 추가/삭제/체결 이벤트 스트림

### ❌ 왜 제외했나

```
핵심 문제: 프로젝트 성격이 바뀜

1. 본질 변질:
   - Order Book 구현 → 데이터 파싱 프로젝트
   - Matching engine ❌ → 로그 재생 ⭕

2. 동시성 스토리 희석:
   - 실제로는 파싱에 시간 대부분 소비
   - "락 최적화"가 아닌 "I/O 최적화"로 변질

3. CV 관점:
   ❌ "ITCH 파서 + Order Book"
      → "그럼 동시성은 어디 있나요?"
   ✅ "Multi-threaded Order Book"
      → 명확한 포지셔닝
```

### 🔮 언제 추가할 수 있나

**조건:**
- 프로젝트를 "실전 시뮬레이터"로 확장할 때
- 실제 데이터로 검증이 필요할 때

**구현 방법:**
```cpp
class ITCHParser {
public:
    void parse_file(const std::string& filename) {
        // Binary parsing
        // → OrderBook에 replay
    }
};

// 사용
ITCHParser parser;
parser.parse_file("20230101.NASDAQ_ITCH50");
// → 수백만 주문 재생 → 성능 검증
```

**예상 공수:** 1주

**참고:**
- kautenja_ref에 이미 구현되어 있음
- 필요 시 참고 가능

### 💡 면접 답변

```
"ITCH 파서는 의도적으로 제외했습니다.
이걸 추가하면 프로젝트가 '데이터 파싱'으로 보일 수 있고,
제 핵심인 '동시성 설계'가 묻힐 수 있어서요.

만약 실제 NASDAQ 데이터로 검증이 필요하다면,
kautenja의 구현을 참고해서 1주 안에 추가할 수 있습니다."
```

---

## 3. Lock-free 자료구조

### 📖 설명

- Mutex 없이 `std::atomic` + CAS로 동시성 제어
- 이론상 최고 성능
- Compare-And-Swap (CAS) 연산 활용

### ❌ 왜 제외했나

```
핵심 문제: 설명 책임이 너무 무거움

1. 구현 난이도:
   - ABA problem 해결 필요
   - Memory ordering (acquire/release/seq_cst)
   - Hazard pointers for memory reclamation

2. 디버깅 지옥:
   - Race condition이 재현 불가능한 경우 많음
   - Valgrind/TSan도 한계 있음

3. 면접 리스크:
   ❌ "Lock-free 썼어요"
      면접관: "ABA problem은 어떻게 해결했나요?"
      면접관: "Memory ordering 설명해보세요"
      면접관: "왜 seq_cst가 아니라 acquire인가요?"
      → 답 못하면 즉시 신뢰도 하락

   ✅ "Read-Write lock으로 충분했습니다"
      면접관: "왜 lock-free 안 썼나요?"
      당신: "Trade-off 고려했는데..." (설명 가능)
```

### 🔮 언제 추가할 수 있나

**조건:**
- Read-Write lock으로 병목이 여전히 남을 때
- Lock-free 자료구조를 깊이 학습한 후
- 실전 필요성이 명확할 때

**구현 예시 (매우 간단한 경우):**
```cpp
// Lock-free queue (단순화)
template<typename T>
class LockFreeQueue {
private:
    struct Node {
        T data;
        std::atomic<Node*> next;
    };
    
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    
public:
    void enqueue(const T& value) {
        Node* node = new Node{value, nullptr};
        Node* prev_tail = tail_.exchange(node);
        prev_tail->next.store(node);
        // ⚠️ 실제로는 이것보다 훨씬 복잡!
    }
};
```

**예상 공수:** 2-4주 + 디버깅

**난이도:** ⭐⭐⭐⭐⭐

### 💡 면접 답변

```
"Lock-free도 고려했지만 의도적으로 제외했습니다.

이유:
1. Read-Write lock으로 7배 개선 달성
2. Lock-free는 구현 복잡도가 매우 높음
3. ABA problem, memory ordering 등 미묘한 이슈 많음
4. 실전에서도 대부분 경우 read-write lock으로 충분

Trade-off를 고려했을 때,
복잡도 대비 효과가 명확하지 않아 보류했습니다.

만약 프로파일링 결과 lock이 여전히 병목이라면,
특정 부분(예: best price cache)만 lock-free로 시도해볼 수 있습니다."
```

---

## 4. 나노초 단위 초고성능 (HFT)

### 📖 설명

- "24ns latency", "60ns matching" 같은 극한 성능
- CPU cache line 정렬
- NUMA awareness
- RDTSC 사용한 정밀 측정

### ❌ 왜 제외했나

```
핵심 문제: 주니어/학생 CV에서 신뢰도 하락

1. 측정 환경 질문:
   면접관: "어떤 CPU에서 측정했나요?"
   면접관: "24ns가 무슨 연산인가요?"
   면접관: "어떻게 측정했나요? (rdtsc? chrono?)"
   → 답변 막히면 전체 프로젝트 신뢰도 추락

2. 실전성:
   - 절대 수치는 환경 의존적
   - 개선율이 더 중요

3. CV 관점:
   ❌ "24ns latency 달성!"
      → 의심스러움
   ✅ "Coarse lock 대비 7배 개선"
      → 신뢰 가능
```

### 🔮 언제 추가할 수 있나

**조건:**
- 실제 HFT 회사 지원 시
- 측정 환경을 완벽히 제어 가능할 때
- 최적화 과정을 모두 설명 가능할 때

**최적화 방법:**
```cpp
// Cache line alignment
struct alignas(64) Order {  // 64-byte cache line
    uint64_t id;
    // ...
};

// Memory pool
class OrderPool {
    std::array<Order, 10000> pool_;  // Pre-allocated
    // → malloc 오버헤드 제거
};

// RDTSC for precise measurement
inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
```

**예상 공수:** 1-2주

### 💡 면접 답변

```
"나노초 단위 최적화는 의도적으로 하지 않았습니다.

이유:
1. 절대 수치는 환경에 크게 의존적
2. CV에서는 '개선 과정'이 더 중요
3. 측정 환경 제어가 어려움

대신 '상대적 개선'에 집중했습니다:
- Step 2 → Step 3: 7배 개선
- 스레드 증가 시 scalability 확인

실전에서 극한 최적화가 필요하다면,
1. Profiling으로 병목 확인
2. Cache locality 개선
3. Memory allocation 최적화
이런 순서로 접근하겠습니다."
```

---

## 5. CppTrader 직접 확장

### 📖 설명

- kautenja/CppTrader 코드베이스를 가져와서
- 거기에 멀티스레딩 추가

### ❌ 왜 제외했나

```
핵심 문제: "남의 설계"를 설명 못함

1. 코드 복잡도:
   - Custom allocator
   - Intrusive containers
   - Template metaprogramming
   → 이걸 다 이해했다고 주장할 수 없음

2. 면접 리스크:
   면접관: "왜 이 함수는 이렇게 설계했나요?"
   당신: "원래 있던 코드라..."
   → 신뢰도 추락

3. CV 관점:
   ❌ "CppTrader 기반으로 확장"
      → Fork & modify
   ✅ "처음부터 구현"
      → 모든 설계 결정 설명 가능
```

### 🔮 언제 추가할 수 있나

**전제:** 절대 안 함!

**대신:**
- CppTrader는 "학습 자료"로만 활용
- 개념을 배우고 자기 코드로 재구현
- 참고는 OK, 복사는 절대 NG

### 💡 면접 답변

```
"CppTrader를 참고했지만, 코드는 복사하지 않았습니다.

이유:
1. 모든 설계 결정을 제가 설명할 수 있어야 함
2. CV 프로젝트는 '학습 과정'을 보여주는 것
3. 복잡한 코드를 가져오면 디버깅도 어려움

참고한 것:
- Order Book의 개념 (price-time priority)
- Matching engine의 로직
- 자료구조 선택 (하지만 단순화)

제 구현:
- std::map, std::vector 같은 표준 컨테이너
- 명확하고 간단한 구조
- 모든 부분을 설명 가능"
```

---

## 6. 다중 매칭 스레드

### 📖 설명

- Matching engine 자체를 병렬화
- 여러 스레드가 동시에 주문 매칭

### ❌ 왜 제외했나

```
핵심 문제: Matching은 본질적으로 순차적

1. Correctness 이슈:
   - Price-Time Priority를 보장하려면 순서 중요
   - 병렬화하면 시간 순서 깨짐

2. 복잡도 폭증:
   - Transaction 개념 필요
   - Rollback 메커니즘
   - 분산 시스템 수준의 동기화

3. 실전성:
   - 실제 거래소도 matching은 단일 스레드
   - 대신 심볼별로 분리
```

### 🔮 언제 추가할 수 있나

**대안: Symbol별 독립 처리** (더 현실적)

```cpp
class MultiSymbolExchange {
private:
    // 각 심볼마다 독립된 OrderBook + 스레드
    std::unordered_map<uint32_t, OrderBook> books_;
    std::unordered_map<uint32_t, std::thread> workers_;
    
public:
    void add_order(const Order& order) {
        uint32_t symbol = order.symbol_id;
        // 해당 심볼의 큐에 추가
        queues_[symbol].push(order);
    }
};

// 각 심볼은 독립적으로 처리
// → 완전한 병렬 처리 가능
```

**예상 공수:** 3-4일

### 💡 면접 답변

```
"Matching engine 자체를 병렬화하는 건 고려했지만 제외했습니다.

이유:
1. Matching은 본질적으로 순차적 (Price-Time Priority)
2. 병렬화하면 correctness 증명이 매우 어려움
3. 실전에서도 matching은 단일 스레드가 표준

대신 실전적인 접근:
- Symbol별 독립 처리
- 각 심볼마다 별도 OrderBook + 스레드
- AAPL과 TSLA는 완전 병렬 처리

이게 더 단순하고, 안전하고, 확장성도 좋습니다."
```

---

## 7. 추가 고려 사항

### 7.1 Persistence & Recovery

**설명:**
- Order book 상태를 디스크에 저장
- Crash 후 복구

**제외 이유:**
- 동시성 프로젝트의 범위 벗어남
- 파일 I/O가 주요 관심사로 변질

---

### 7.2 Market Data 배포

**설명:**
- WebSocket/FIX로 market data 스트리밍
- 실시간 price feed

**제외 이유:**
- 네트워크 프로그래밍 프로젝트로 변질
- 핵심(Order Book)이 묻힘

---

### 7.3 GUI/Dashboard

**설명:**
- 실시간 Order Book 시각화
- Web interface

**제외 이유:**
- Frontend 프로젝트가 아님
- Backend 시스템이 강점

---

## 🎯 핵심 메시지

> **"무엇을 하지 않았는가도 중요한 설계 결정이다"**

### 면접에서 강조할 점

```
1. ✅ "이것들도 고려했습니다"
   → 시야가 넓음을 보여줌

2. ✅ "하지만 의도적으로 제외했습니다"
   → 우선순위를 정할 줄 앎

3. ✅ "Trade-off를 고려했습니다"
   → 엔지니어링 사고방식

4. ✅ "필요하면 추가할 수 있습니다"
   → 확장 가능성 이해
```

### CV/포트폴리오 관점

```
❌ "기능 30개 구현했어요"
   → 깊이가 없어 보임

✅ "핵심 3개를 완벽하게 구현했고,
   고급 기능들은 Trade-off 고려해서 제외했습니다"
   → 성숙한 엔지니어로 보임
```

---

## 📚 참고 자료

- **Lock-free 자료구조**: "The Art of Multiprocessor Programming"
- **HFT 최적화**: "Trading at the Speed of Light"
- **실전 Order Book**: NASDAQ ITCH specification

---

**마지막 한 마디:**

> "이 문서가 있다는 것 자체가,
> 당신이 단순히 코드를 짠 게 아니라
> 설계를 고민했다는 증거입니다."

면접에서 "더 개선할 수 있나요?" 질문이 나오면,
이 문서를 언급하세요. 큰 플러스가 될 겁니다. 💪

