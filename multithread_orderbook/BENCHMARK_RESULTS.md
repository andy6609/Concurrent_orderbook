# 벤치마크 결과 활용 가이드

## 🎯 벤치마크 결과가 있으면 할 수 있는 것들

### 1. **README에 실제 숫자 넣기** ✅
현재 README는 예상 수치만 있음 → 실제 측정값으로 교체

### 2. **성능 비교 그래프 생성** 📊
- Step 2 vs Step 3 비교 차트
- Scalability curve
- CV/포트폴리오에 추가 가능

### 3. **CV에 구체적 수치 기재** 💼
- "7배 개선" → "470K → 3.3M ops/sec"
- 면접에서 실제 데이터로 설명

### 4. **성능 분석 문서 작성** 📝
- 병목 분석
- 최적화 효과 정량화

---

## 📋 벤치마크 실행 방법

### Step 1: Correctness Test
```bash
cd step1_baseline
mkdir build && cd build
cmake ..
make
./test_correctness
```

**예상 결과:**
```
All tests PASSED! ✓
```

---

### Step 2: Coarse-Grained Lock Benchmark
```bash
cd step2_coarse_lock
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./bench_contention
```

**실제 측정 결과:**
```
Threads: 1 | Total ops: 50000 | Time: 10ms | Throughput: 5000000 ops/sec | Avg latency: 200 ns
Threads: 2 | Total ops: 100000 | Time: 25ms | Throughput: 4000000 ops/sec | Avg latency: 250 ns
Threads: 4 | Total ops: 200000 | Time: 46ms | Throughput: 4347826 ops/sec | Avg latency: 230 ns
Threads: 8 | Total ops: 400000 | Time: 129ms | Throughput: 3100775 ops/sec | Avg latency: 322 ns
```

**측정값:**

| Threads | Throughput (ops/sec) | Avg Latency (ns) |
|---------|---------------------|------------------|
| 1       |                     |                  |
| 2       |                     |                  |
| 4       |                     |                  |
| 8       |                     |                  |

---

### Step 3: Read-Write Lock Benchmark
```bash
cd step3_rwlock
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./bench_scaling
```

**실제 측정 결과:**
```
Threads: 1 | Total ops: 100000 | Time: 12ms | Throughput: 8333333 ops/sec | Avg latency: 120 ns
Threads: 2 | Total ops: 200000 | Time: 35ms | Throughput: 5714285 ops/sec | Avg latency: 175 ns
Threads: 4 | Total ops: 400000 | Time: 93ms | Throughput: 4301075 ops/sec | Avg latency: 232 ns
Threads: 8 | Total ops: 800000 | Time: 532ms | Throughput: 1503759 ops/sec | Avg latency: 665 ns
```

**측정값:**

| Threads | Throughput (ops/sec) | Avg Latency (ns) |
|---------|---------------------|------------------|
| 1       |                     |                  |
| 2       |                     |                  |
| 4       |                     |                  |
| 8       |                     |                  |

---

## 📊 결과 분석 및 활용

### 1. 성능 비교 표 만들기

벤치마크 결과를 받으면 아래 형식으로 정리:

```markdown
## Performance Comparison

| Implementation | Threads | Throughput | vs Baseline | vs Step 2 |
|----------------|---------|------------|-------------|-----------|
| Step 1 (Baseline) | 1 | (미측정) | - | - |
| Step 2 (Coarse) | 1 | 5,000,000 ops/sec | - | 1.0x |
| Step 2 (Coarse) | 4 | 4,347,826 ops/sec | - | 0.87x |
| Step 2 (Coarse) | 8 | 3,100,775 ops/sec | - | 0.62x |
| Step 3 (RW Lock) | 1 | 8,333,333 ops/sec | - | 1.67x |
| Step 3 (RW Lock) | 4 | 4,301,075 ops/sec | - | 0.99x |
| Step 3 (RW Lock) | 8 | 1,503,759 ops/sec | - | 0.48x |

**주요 관찰:**
- **1 스레드**: Step 3가 Step 2보다 1.67배 우수 (read-write lock의 이점)
- **8 스레드**: Step 2가 Step 3보다 우수 (shared_mutex 오버헤드)
- **분석**: 이 벤치마크는 write 비율 30%로 높아서, read-write lock의 이점이 제한적
```

### 2. Scalability 계산

```
Step 2 Scalability:
  8 threads / 1 thread = X.Xx (이상적: 8.0x)

Step 3 Scalability:
  8 threads / 1 thread = X.Xx (이상적: 8.0x)

Step 3 vs Step 2 (8 threads):
  Step 3 throughput / Step 2 throughput = X.Xx
```

### 3. README 업데이트

실제 측정값으로 README의 예상 수치 교체:

```markdown
### 벤치마크 결과

**Step 2 (Coarse-grained lock):**
- 1 thread: XXX ops/sec
- 8 threads: XXX ops/sec (X.Xx scalability)

**Step 3 (Read-Write lock):**
- 1 thread: 8,333,333 ops/sec (Step 2 대비 1.67배)
- 8 threads: 1,503,759 ops/sec (Step 2 대비 0.48배)
- **주의**: 이 워크로드에서는 스레드 증가 시 오히려 성능 저하
- Read-write lock은 read-heavy workload에서 효과적이지만, 이 벤치마크는 write 비율이 높아 오버헤드가 큼
```

### 4. CV에 추가할 내용

```
Multi-threaded Order Book Optimization
- Measured lock contention in coarse-grained implementation
- Optimized using read-write locks (shared_mutex)
- Achieved X.Xx throughput improvement (XXX → XXX ops/sec)
- Scalability improved from X.Xx to X.Xx with 8 threads
```

### 5. 성능 그래프 생성 (선택사항)

Python으로 간단한 그래프:

```python
import matplotlib.pyplot as plt

threads = [1, 2, 4, 8]
step2_throughput = [XXX, XXX, XXX, XXX]  # 실제 측정값
step3_throughput = [XXX, XXX, XXX, XXX]  # 실제 측정값

plt.plot(threads, step2_throughput, 'o-', label='Coarse-grained')
plt.plot(threads, step3_throughput, 's-', label='Read-Write Lock')
plt.xlabel('Number of Threads')
plt.ylabel('Throughput (ops/sec)')
plt.title('Order Book Performance Comparison')
plt.legend()
plt.grid(True)
plt.savefig('performance_comparison.png')
```

---

## 🎯 면접에서 활용

### 실제 데이터로 설명

```
"벤치마크 결과:
- Step 2 (coarse lock): 1 스레드에서 5.0M ops/sec, 8 스레드에서 3.1M ops/sec
- Step 3 (read-write lock): 1 스레드에서 8.3M ops/sec, 8 스레드에서 1.5M ops/sec

**관찰:**
- 1 스레드: Read-write lock이 1.67배 우수
- 8 스레드: 오히려 Step 2가 더 나음 (shared_mutex 오버헤드)

**분석:**
이 벤치마크는 write 비율이 30%로 높아서, read-write lock의 이점이 제한적입니다.
실제 거래소 시나리오(읽기 90%+)에서는 read-write lock이 더 효과적일 것입니다."
```

---

## 📝 체크리스트

벤치마크 실행 후:

- [ ] Step 1 테스트 통과 확인
- [ ] Step 2 벤치마크 결과 기록
- [ ] Step 3 벤치마크 결과 기록
- [ ] 성능 비교 표 작성
- [ ] README 업데이트
- [ ] CV 내용 업데이트
- [ ] (선택) 성능 그래프 생성

---

**벤치마크 결과를 받으면 이 문서를 업데이트하고, README와 CV에 반영하세요!**

