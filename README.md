# Hospital Resource Simulator

A simulation of a hospital emergency department, written in C99 with
[raylib](https://www.raylib.com/). Patients arrive at random, wait in a queue
ordered by how urgent they are, and are treated by nurses and doctors in a
limited number of beds. Some are sent on to X-ray, MRI, theatre, the ward and
other rooms, each with its own queue. The idea is to see how staff, beds and
equipment affect waiting times, shift by shift.

Advanced Programming group project, SETU, by Ryan Mulcahy (C00315272) and
Kobi Bourne (C00249676). Built with the help of AI (Claude, by Anthropic), as
recorded in our development journals.

## Building and running

You need raylib and a C compiler. The same `makefile` works on Mac and Windows.

**Mac** (raylib from Homebrew: `brew install raylib`)

```
make run
```

**Windows** (raylib's w64devkit, which includes gcc and `mingw32-make`)

```
mingw32-make run
```

On Windows, type `mingw32-make` wherever this page says `make`, for example
`mingw32-make test`. If raylib isn't in `C:/raylib`, give the path to its
`bin` folder: `mingw32-make run RAYLIB_BIN=D:/raylib/w64devkit/bin`

Run it from the project folder, so it can find `conditions.csv` and `logo.png`.

| Command (Mac) | Command (Windows) | What it does |
|---|---|---|
| `make` | `mingw32-make` | build the program |
| `make run` | `mingw32-make run` | build it and run it |
| `make test` | `mingw32-make test` | build and run the automatic tests (no window) |
| `make clean` | `mingw32-make clean` | delete the program, the test program and the log CSV files |

## Using it

Each day starts with the **setup screen**: choose the nurses and doctors for
each shift, the beds, the equipment, the kind of day (normal, flu season or a
major incident), on-call doctors, power cuts, and whether yesterday's patients
carry over. Then the **roster** shows who's on, and the shift starts.

A day is two 12-hour shifts, day (07:00 to 19:00) and night (19:00 to 07:00).
At 1x speed a shift lasts 3 minutes. Each shift ends with a **report**: patients
seen, waiting times, how busy the staff, beds, rooms and equipment were, and
graphs of the waiting room through the shift.

### Keys during a shift

| Key | Action |
|---|---|
| SPACE | pause |
| UP / DOWN | speed up / slow down |
| V | switch between the 2D and 3D views |
| E | an emergency arrives |
| H | a helicopter rescue |
| C | cut the power |
| G | the mini-games menu (or O, S, X, P to go straight into one) |
| M | sound on or off |
| R | restart the day |
| F11 | full screen |

### In the 3D view

| Control | Action |
|---|---|
| left-drag | turn the camera |
| right-drag | move the camera |
| scroll | zoom |
| 0 | the whole floor |
| 1 to 7 | the rooms on the floor |
| 8 | A&E |
| 9 | the helipad and ambulance bay |
| F, PAGE UP / PAGE DOWN | change floor, or see the outside |
| T | a cinematic tour |
| click a patient | follow them |

### On the report

| Key | Action |
|---|---|
| L | patients treated (scroll with the wheel or UP / DOWN) |
| E | equipment and rooms |
| G | graphs |
| B | back to the report |
| N | next shift |

Every shift's treatment log is also saved as a CSV file that opens in Excel,
for example `log_day1_night.csv`: one row per patient, with their condition
and ICD-10 code, arrival and discharge times, wait, who treated them, where
they were sent and whether they came by helicopter.

## How it works

- **Arrivals.** Patients arrive at random, more often by day than at night,
  with a severity of 1 (emergency), 2 (urgent) or 3 (routine). Each gets a real
  condition from `conditions.csv` that matches their severity, with its ICD-10
  code and the rooms they'll need afterwards (for example `xray` or
  `theatre+ward`).
- **The waiting queue** is a linked list kept in order: most urgent first, and
  first come, first served within the same severity (`queue_insert` in
  `patient.c`).
- **A&E.** A free bed takes the patient at the front of the queue. A nurse
  assesses them and either treats them or sends them to a doctor. Treatment
  may need equipment (monitors, ventilators, defibrillators, drips), which is
  shared and can run out.
- **After A&E,** patients go to their rooms in turn, queuing for each one, and
  then home. Patients who can't walk need a wheelchair and a porter.
- **Events:** helicopter rescues (coast guard, search and rescue, fire service)
  jump the queue, on-call doctors come in when the waiting room is full, and
  power cuts stop the scanners and lifts until the generator starts.
- **The treatment log** is a dynamic array that doubles in size with `realloc`
  as patients are discharged. All memory is freed at the end of each day.

The simulation code (`sim.c`, `patient.c`, `conditions.c`) never draws
anything; the drawing code only reads the simulation's state. That keeps the
logic testable without a window.

## Tests

`make test` (`mingw32-make test` on Windows) runs `tests/test_core.c`: 18 tests of the core C code, with no
window. They check:

1. the waiting queue's order, including inserting at the front, middle and end
2. the conditions loader, with good files, Windows line endings, broken lines,
   over-long names and a missing file
3. the treatment log growing past 64 records, and being reused at handover
4. that no patient is ever lost or in two places at once, on every kind of day,
   through power cuts and with patients carried over
5. the CSV export, including failing safely when it can't write the file
6. that `sim_free` empties every list, and three whole days run in a row

On a Mac, `leaks --atExit -- ./run_tests` also checks for memory leaks.

## Files

| File | What's in it |
|---|---|
| `main.c` | window, main loop and keyboard input |
| `patient.c/.h` | the patient record and the waiting queue |
| `sim.c/.h` | the simulation: arrivals, treatment, rooms, equipment, rescues, power cuts, the shift clock, the treatment log |
| `conditions.c/.h`, `conditions.csv` | loading the conditions and their ICD-10 codes |
| `roster.c/.h` | the staff rota and the roster screen |
| `setup.c/.h` | the setup screen before each day |
| `draw.c/.h` | the 2D view |
| `report.c/.h` | the end-of-shift report and its pages |
| `view3d.c/.h`, `view3d_parts.h` | the 3D view of A&E, and the drawing pieces shared by the 3D files |
| `rooms3d.c/.h` | the rest of the ground floor in 3D |
| `floors3d.c/.h` | the first and second floors in 3D, and the lifts |
| `outside3d.c/.h` | the outside, the car park and the grounds |
| `minigames.c/.h`, `*game.c/.h`, `gameui.c/.h` | the mini-games and their menu |
| `sound.c/.h` | sound effects, made in code |
| `tests/test_core.c` | the automatic tests |

## Who did what

We split the work by feature, each on our own branch, and every change went
into `main` through a pull request that the other person tested (one on Mac,
one on Windows) and merged. Our development journals have the details, day by
day.

**Ryan:** split the starter program into modules; day and night shifts with
end-of-shift reports; the conditions file with ICD-10 codes; the treatment log,
its CSV export and the patients-treated page; the ground-floor rooms in 3D and
the camera controls; the automatic tests; reviewing and merging Kobi's work.

**Kobi:** the makefile for Mac and Windows; the look of the 2D view and the
logo; doctors, then nurses who assess every patient; the staff roster and
names, and night-shift staffing; the 3D view; the rooms as part of the
simulation; ambulances and helicopter rescues; the upper floors and outside;
the setup screen, equipment, power cuts, on-call doctors and graphs; the
mini-games and sound; reviewing and merging Ryan's work.

## Video tour

A tour of the hospital in 3D (press T in the 3D view for the same tour):

[▶ Watch the video tour](https://setuo365-my.sharepoint.com/:v:/g/personal/c00315272_setu_ie/IQCYrhV4zTtfSI2P8LxMlqJEAVa9I0qK8oMNCPsi1B-poqw?nav=eyJyZWZlcnJhbEluZm8iOnsicmVmZXJyYWxBcHAiOiJPbmVEcml2ZUZvckJ1c2luZXNzIiwicmVmZXJyYWxBcHBQbGF0Zm9ybSI6IldlYiIsInJlZmVycmFsTW9kZSI6InZpZXciLCJyZWZlcnJhbFZpZXciOiJNeUZpbGVzTGlua0NvcHkifX0&e=0AjyHF)
