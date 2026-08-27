# LINKS
VIDEO DEMO OF THE GRAPH IN TASK 1: https://drive.google.com/file/d/1rtJFucZZG-jzJXR667OVSMjyXvwqLdK8/view?usp=sharing

TINKERCAD LINK: https://www.tinkercad.com/things/jfOQjMdPOwV-adyanshupradhan2026b2ps1353h?sharecode=xFk-bbgp52ltXBOd__MkTreDqqn2OcCLENuQBg0nJzA

--------------------------------------------------------------------------------------------------------------------------

# Task 1: Keeping Watch Over Odysseus

>*"Odysseus and his crew are fated to make it back to Ithaca."*

>Unfortunately they are not, only Odysseus was fated to return back to Ithaca... but I suppose are as Athena's intern we are tasked with the minor task of changing fate itself with our ***proprietary real-time depth monitoring dashboard built with Streamlit and Plotly, designed to ingest all sorts of noisy, error riddled sensor feed, clean it up, smooth it out, and sound the alarm before the hull meets the seabed.™***

![Athena](https://media1.tenor.com/m/46cPe90oJdwAAAAC/aeolus-gigi.gif)

## How I Approached the Task

### 1. Grabbing the Data
```python
df = pd.read_csv(path)
df["depth_raw"] = pd.to_numeric(df['Depth (m)'], errors="coerce")
```
Real world sensors are often prone to errors, so rather than let a stray `#VALUE!` cell crash the whole dashboard, I created a new column `depth_raw` that takes numeric values from `Depth (m)`, and lets anything unparseable collapse safely to `NaN`.

Every one of these "error values" is tracked and kept note of for later. 

### 2. Handling Erratic & Corrupted Data
There two classes of bad data provided by the sensor and both are handled differently:
| Type | Problem | Detection |
|---|---|---|
| **Missing / non-numeric** (`#VALUE!`, blanks) | Becomes `NaN` after parsing | Flagged as `error_values`  |
| **Erratic spikes/dips** | A value physically implausible given its neighbors | Flagged via rolling median + MAD outlier detection |

Both flag types are then fed into the same downstream and fixed using interpolation.

```python 
    df["is_outlier"] = df["is_outlier"] | df["error_values"] #if value was detected as an outlier or it already had a error flag
    d = df["depth_raw"].where(~df["is_outlier"])
    df["depth_clean"] = d.interpolate(limit_direction="both")
```
#### Logic behind Rolling Median & Median Absolute Deviation

- **Rolling Median**

$$\text{med}_i = \text{median}(depth_{i-w/2}, \dots, depth_{i+w/2})$$

A local "expected value" at each point is computed from a window of w neighbors.

- **Residual**

$$resid_i = |depth_i - \text{med}_i|$$

How far each raw point strays from what its neighborhood says it should be.

- **Median Absolute Deviation (MAD)**

Using a similar window logic to predict a local "expected median". This is similar to the conventionally used standard deviation, but we are using medians instead because a single wild spike can drag a standard deviation calculation way off, but it barely nudges a median.

- **Modified z-score**

A usual z-score measures how many standard deviations a value is away from the mean of its dataset. As mentioned before we are using medians instead of avaerages to not be thrown off by the very outliers we are trying to catch.

The following formula is being used:

$$z_i = \frac{resid_i}{1.4826 \times MAD_i}$$

The constant 1.4826 rescales MAD so that it estimates the same spread a standard deviation would, i.e. making the resulting `z` comparable to a familiar "number of standard deviations away" score. A point is flagged as an outlier when `z > k` (the sensitivity slider).

```python
med = depth.rolling(window, center=True).median()
resid = (depth - med).abs()
mad = resid.rolling(window, center=True).median()
z = resid / (1.4826 * mad)
flag = z > k
```
### Noise Reduction 

Even after stripping outliers, the sensor noise remains. Two smoothing options are offered side-by-side (Both are implemented from first principles, no external filtering library):

- **EMA (Exponential Moving Average)** - uses a simple set parameter (`α`).

$$\hat{x}_i = \alpha \cdot x_i + (1-\alpha)\cdot \hat{x}_{i-1}$$

Each smoothed point is a blend of the new raw reading and the previous smoothed estimate. A higher `α` trusts new data more (more responsive), whereas a lower `α` leans on history (smoother). It's a recursive weighted average where old readings' influence subsequent readings.

```
def ema_filter(x: np.ndarray, alpha: float) -> np.ndarray:
    out = np.empty_like(x, dtype=float)
    out[0] = x[0]
    for i in range(1, len(x)):
        out[i] = alpha * x[i] + (1 - alpha) * out[i - 1]
    return out
```

- **Kalman filter** - It's a smarter filter than EMA that balances process noise (how much the true depth is expected to change (`Q`)) against measurement noise (how much to trust the sensor (`R`)), continuously updating its confidence (`p`) as it goes.

Briefly, here's how it works:

**First, we make an intial prediction:** $P_i = P_{i-1} + Q$ 

Time has passed since the last reading, so the ship could've drifted. Confidence naturally erodes a bit before new evidence comes in. Q controls how fast the confidence changes. A bigger Q means "I expect the real depth to change a lot between readings," so uncertainty grows faster.

**Next, decide how much to trust the new reading:** $K_i = \dfrac{P_i}{P_i + R}$

R is how noisy/unreliable you think the sensor is. K comes out as a dial between 0 and 1: if the sensor is trustworthy (R small), K leans toward 1 and the new reading matters a lot, whereas if the sensor is bad (R large), K leans toward 0 and the filter mostly ignores it, sticking with its own prediction.

**Then, update estimate:** $\hat{x}_i = \hat{x}_{i-1} + K_i\,(x_i - \hat{x}_{i-1})$

Takes the old guess and moves it partway toward the new reading, how far depends entirely on `K`.

**Finally, shrink uncertainty:** $P_i = (1-K_i)\,P_i$

Having just plotted a new data point, uncertainty shrinks a little.


Unlike EMA's fixed `α`, `K` adapts on every step based on the running uncertainty `P`.

### Presentation & Animation
- **Streamlit:** Using for the UI. Have included a sidebar controls for configurable playback speed, filter choice, sensitivity, and grounding threshold.
- **Plotly:** renders the depth-time graph with the raw signal (faded), corrupted points (marked with red ✕), and the filtered signal (bold green) all layered together.
- **Animation Logic:** 
For animating Streamlit just re-runs our entire script top-to-bottom every time something changes:
 
1. All the depth data is loaded and cleaned upfront (`load_raw` + `clean_series`), but only sliced up to the current playhead index:

```python
   idx = min(st.session_state.idx, n_total)
   window = processed.iloc[:idx].copy()
```
   The graph is redrawn from scratch each run, but only ever shows data up to `idx`, so visually it looks like new points are dynamically being added in.
 
2. `st.session_state` remembers the playhead across reruns. Streamlit normally forgets all local variables between reruns (it's a fresh script execution every time), so `idx` and whether the animation is `running` are stashed in `st.session_state`, which persists across those reruns for the same session.

3. At the very end of the script:

```python
   if st.session_state.running and idx < n_total:
       time.sleep(1.0 / speed)
       st.session_state.idx += 1
       st.rerun()
```
   This is the actual "tick" of roughly one second, upon which we bump the playhead forward by one reading, then explicitly force Streamlit to rerun the whole script immediately. And since it's a loop, it drives itself forward one rerun at a time.

4. Play / Pause / Reset: Clicking "Play" sets `running = True` and lets the above loop above take over. "Pause" sets it `False`, so the next rerun simply stops scheduling further reruns and the graph freezes wherever it is. "Reset" snaps `idx` back to `1` and stops playback, making the graph ready to start over.

5. Speed control: The sidebar `speed` slider directly scales the `time.sleep()` delay (`1.0 / speed`), so "4×" speed means a quarter-second pause between ticks instead of a full second.


# Task 2: Keeping Watch Over Odysseus

> *"Athena wants an easier way to monitor Odysseus and his crew without watching over them herself."*\
> In this task we again answer Athena's call as her unpaid intern with an Arduino-based monitoring system that watches the sea for you.

![Athena](https://media1.tenor.com/m/AMy6LtBlm1UAAAAd/athena-epic-the-musical.gif)

## Hardware Used
 
| Component | Pin(s) | Role |
|---|---|---|
| Arduino Uno | — | The Brain (a sober one, unlike the crew) |
| HC-SR04 Ultrasonic Distance Sensor | `TRIG=9`, `ECHO=10` | 	Detects Charybdis closing in |
| LDR (Light Sensor) | `A0` | Detects the sky darkening into a storm |
| 16x2 LCD | `12, 11, 5, 4, 3, 2` | Display |
| Push Button | `8` | Drops/raises anchor |
| LED | `7` | Blinks during Storm |
| Buzzer | `6` | Sounds during Charybdis, on Wreck and, on Boot |

## How I Approached the Task

The task can be mapped into **three parts**:
1. **The Sensing part** - Reading raw data from the distance sensor, LDR, and button (with debouncing).
2. **The Decision making / Logic part** - After getting all the raw data we need the logic that decides what state we're in, tracking the 5-second danger timer and finally telling us when we wreck the ship.
3. **The Output part** - LCD text, LED blinking, buzzer tones, using everything we have to inform Odysseus that he's screwed (or not).

Each of the requirements, problems encountered and creative liberties are broken down below
### 1. Thresholds 
> *"Storm below half, Charybdis below 100cm"*
```cpp
const int   LIGHT_THRESHOLD_PCT = 92;
const float DIST_THRESHOLD_CM   = 100;
```
The threshold for distance works as expected but as you see the LDR threshold is set at 92 instead of 50. This is because the LDR in this circuit doesn't respond linearly to light, the physical slider's actual half-brightness point mapped to roughly 92% on the 0–100 scale after map().

So instead of hardcoding 50, I calibrated the threshold to match real sensor behavior.

### 2. Custom LCD Characters
```cpp
byte anchorIcon[8] = {...};
byte skullIcon[8]  = {...};
byte shipLeft[8]   = {...};
byte shipRight[8]  = {...};
```
The LCD is only 16x2 text. Unsurprisingly it's hard to make the UI feel thematic with just 32 characters. So I hand-drew 5x8 pixel-grid charecters (anchor, skull, and a ship) and registered them as custom characters (`lcd.createChar`) in `setup()` to make the UI befitting of the Odyssey theme.

### 3. "The timer shouldn't reset, just continue"

> *"If Storm and Charybdis are triggered at the same time, whichever state is entered first remains active and its timer continues."*\
> *"Dropping the anchor... resets the timer."*

My approach was to treat "danger" as one continuous condition, and not as two competing timers.

- `stormActive` / `charybdisActive` are independent booleans updated every loop. Therefore both can be `true` simultaneously.
- `inDangerNow = stormActive || charybdisActive` collapses them into a single danger flag.
- `dangerStartTime` is stamped once, only on the transition from "not in danger" → "in danger" (`!wasInDanger && inDangerNow`). While any danger persists whether it's the same calamity, a second calamity joining, or one calamity replacing another the timer is never touched again. This satisfies "whichever state is entered first... its timer continues" without having to account for which calamity came first.
- The moment danger fully clears (both flags false), `wasInDanger` goes false, so the next danger event gets a fresh `dangerStartTime`.

#### ***Note for reviewers*: Additionally instead of "whichever state is entered first remains active and its timer continues." I thought about it and personally feel Odysseus should probably be informed when he is stuck in two calamities simultaneously. So in the code if Storm was going on and then Charybdis starts, the LCD would let you know both Storm and Charybdis are going on while the timer of the first calamity (i.e. Storm) continues.**

### 4. Anchor toggling with a push button

A physical button is noisy, i.e. a single press can register as several rapid HIGH/LOW flickers, which would toggle the anchor on and off several times per press.

Therefore, I implemented a debounce pattern, to only accept a state change after it's been stable for `DEBOUNCE_MS` (set to 50 ms). 

```cpp
if (millis() - lastButtonCheck > DEBOUNCE_MS) {
    if (reading != stableButtonState) {
        stableButtonState = reading;
        if (stableButtonState == LOW) pressedEdge = true;
    }
}
```
Anchor safety logic:
```cpp
  if (anchorDropped) {
    currentState = ANCHOR_DROPPED;
    stormActive = false;
    charybdisActive = false;
    wasInDanger = false;
    updateLCD();
    return;
  }
```
While, `anchorDropped` is true, all calamities are set to false, and the ship is protected from any danger. Additionally, setting `wasInDanger` as false resets the timer as well.

### 5. WRECKED
Wrecking logic relies on a couple of things. Firstly, `millis() - dangerStartTime >= DANGER_TIME_MS` checks if 5 seconds have elapsed by taking the difference of current time from the time noted when the danger started.

Also as can be seen in the codeblock below:
```cpp
if (currentState == WRECKED) {
    digitalWrite(PIN_LED, HIGH);
    updateLCD();
    return;
}
```
There is intentionally no code path back out of WRECKED. The only way out is a hardware reset, fulfilling the "the ship remains in that state until the simulation is restarted." condition.

### 6. STORM
First I mapped the LDR's 0–1023 raw range to a 0–100% scale for readability. 
Our trigger here is managed by `lightPct < LIGHT_THRESHOLD_PCT`, i.e. we are checking if the light percentage that the LDR is detecting is less than the threshold light percentage.

If the trigger conditions meet the the countdown starts, and the ship transitions from safe → danger, and is left completely untouched afterward:
```cpp
if (!wasInDanger) {
    dangerStartTime = millis();
}
```

If the storm clears within 5 seconds, `inDangerNow` goes false, `currentState` falls back to `OPEN_SEA`, and `wasInDanger` resets so the next calamity gets a fresh timer.
### 7. CHARYBDIS
Rather than a flat tone, the buzzer pitch is modulated with a sine wave to feel like an actual siren.
```cpp
int pitch = 600 + (int)(200 * sin(millis() / 150.0));
tone(PIN_BUZZER, pitch);
```
Charybdis is triggered when the HC-SR04 detects an object closer than 100 cm (`distanceCm < DIST_THRESHOLD_CM`). The sensor measures distance by sending a trigger pulse and measuring the returning echo pulse.

It also shares the exact same `dangerStartTime` / `wasInDanger` timer mechanism as STORM.

### 8. EXTRA TOUCHES
- A boot animation: a scrolling # progress bar to show the machine coming to life.
- Anchor chime: distinct short tones for dropping (1200Hz) vs raising (900Hz) the anchor.
- Wreck jingle: a jingle plays during wreck, because just text is not dramatic enough on its own.

- **Live metrics dashboard:** shows current depth, rate of change, status, readings processed at a glance. A big red alert fires the moment depth crosses into danger.
