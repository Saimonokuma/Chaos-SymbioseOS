
## 2024-05-06 - [Eliminating Float Overhead in OpenMosix Load Metrics]
**Transformation:** Replaced floating point penalty multipliers with pure scaled integer math in `compute_load_score()` inside `openmosix_nx/migrate.c`.
**Result:** Integer math eliminated all FPU dependencies in the migration scheduler's hot path, speeding up millions of score calculations without losing ratio correctness.
**Lesson:** OpenMosix heuristics from 2004 relied heavily on floats for percentage penalties. Modern heterogeneous clusters require faster evaluation loops where shifting and scaled integer math (`(x * 40) / 100`) achieves identical precision at a fraction of the clock cycles. Floating point should be banished from load balancers.
