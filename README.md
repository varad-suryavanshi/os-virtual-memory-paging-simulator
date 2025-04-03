# 🧠 OS Virtual Memory Paging Simulator - C++

This project simulates a virtual memory system with support for multiple page replacement algorithms. It was developed as part of **Operating Systems Lab 3** coursework. The simulator replicates how an OS handles memory using FIFO, Random, Clock, Aging, NRU, and Working Set algorithms for page replacement.

---

## 📁 Project Structure

```
Lab_3_Submission/
├── Lab_3.cpp          # Main memory simulation logic
├── makefile           # Build script
├── runit.sh           # Script to run simulations and generate output
├── gradeit.sh         # Script to compare your output against reference output
├── Inputs/            # Contains all input files (e.g., in1, rfile, etc.)
├── refout/            # Reference output directory for grading
├── yourout/           # Your output will go here (create this before running)
├── *.log              # Log files (e.g., gradeit.log, make.log)
```

---

## 🛠️ Build Instructions

You can compile the simulator using the provided `makefile`. From the project root:

```bash
make
```

This will produce an executable (usually named `lab3`).

---

## 🚀 How to Run

Create an output directory first:

```bash
mkdir yourout
```

### ▶️ Run the Simulator

Use `runit.sh` to run simulations across all test inputs and algorithms:

```bash
./runit.sh yourout ./lab3
```

This script loops over:
- All inputs in `Inputs/`
- All frame sizes (16 and 31)
- All algorithms (`f r c e a w`)
- And stores output in `yourout/`

You can pass extra options to `lab3` by modifying the `PARGS` in `runit.sh`.

---

## ✅ How to Grade

After generating outputs using `runit.sh`, compare your output against the reference using `gradeit.sh`:

```bash
./gradeit.sh refout yourout gradeit.log
```

This script:
- Compares each output file
- Logs mismatches and diffs into `gradeit.log`
- Summarizes the number of matches per algorithm

You’ll see output like:

```
input  frames   f  r  c  e  a  w
1      16       .  .  .  #  .  .
...
SUM              11 11 10  9 11 11
```

- `.` = Match
- `#` = Mismatch
- `o` = Output file missing

---

## 💡 Supported Algorithms

The simulator supports the following page replacement algorithms:
- `f` – FIFO
- `r` – Random
- `c` – Clock
- `e` – NRU (ESCNRU)
- `a` – Aging
- `w` – Working Set

You can select an algorithm using `-a` flag:
```bash
./lab3 -f16 -aW Inputs/in1 Inputs/rfile
```

---

## 📊 Output Options (Flags)

You can pass the following flags via `-o`:

| Flag | Description                  |
|------|------------------------------|
| O    | Detailed instruction output  |
| P    | Print page tables            |
| F    | Print frame table            |
| S    | Print summary stats          |

Example:
```bash
./lab3 -f16 -aW -oOPFS Inputs/in1 Inputs/rfile
```

---

## 📝 Author

**Varad Suryavanshi**  
---
