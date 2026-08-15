# Research testnet proof (honest)

This file is overwritten by `python3 scripts/prove_research_goals.py` after a
**real** local `additiond` run. The checked-in copy below is a placeholder until
that harness has been executed in this revision.

Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

Do not treat this placeholder as a passing ZK-Shield or invented TPS result.

```bash
cmake -S . -B build -DADDITION_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
python3 scripts/prove_research_goals.py
```
