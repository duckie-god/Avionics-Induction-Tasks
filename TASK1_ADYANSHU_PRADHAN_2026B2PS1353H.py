# pip install streamlit numpy pandas plotly
# To run the app, in the terminal use the command: python -m streamlit run TASK1_ADYANSHU_PRADHAN_2026B2PS1353H.py
# OR if above doesn't work use: streamlit run TASK1_ADYANSHU_PRADHAN_2026B2PS1353H.py
# Streamlit should open at http://localhost:8501
# Use the sidebar for controls

import time
import numpy as np
import pandas as pd
import plotly.graph_objects as go
import streamlit as st

# Page setup
st.set_page_config(page_title="Task 1", layout="wide", page_icon="https://encrypted-tbn0.gstatic.com/images?q=tbn:ANd9GcQk_-n5VdTm7zAn58LQoByytWxBtpfniaCYITHtdVmMRw&s=10")

st.markdown(
    """
    <style>
    .metric-box{background:#0e1117;border:1px solid #262730;border-radius:10px;
                padding:14px 18px;text-align:center;}
    .big-number{font-size:2.2rem;font-weight:700;}
    .danger{color:#ff4b4b;}
    .warn{color:#ffb020;}
    .safe{color:#00d97e;}
    </style>
    """,
    unsafe_allow_html=True,
)

# GRABBING THE DATA
@st.cache_data
def load_raw(path) -> pd.DataFrame:
    """Extracting data from the given Depth_Data.csv. Non-numeric junk (like the '#VALUE!')
    becomes NaN rather than crashing the parser."""
    df = pd.read_csv(path)
    df["depth_raw"] = pd.to_numeric(df['Depth (m)'], errors="coerce")
    df["t"] = list(range((len(df))))  # time in seconds, one reading/sec
    df["error_values"] = df["depth_raw"].isna() #to keep track of error values
    return df.reset_index(drop=True)

#Detecting Outliers
def detect_outliers(depth: pd.Series, window: int = 25, k: float = 5.0) -> pd.Series:
    """Flags erratics spikes/dips using a rolling median + MAD threshold.
    Robust to the fact the signal itself is slowly trending (descending seabed)."""
    med = depth.rolling(window, center=True, min_periods=1).median()
    resid = (depth - med).abs()
    mad = resid.rolling(window, center=True, min_periods=1).median()
    mad = mad.replace(0, np.nan)
    z = resid / (1.4826 * mad)
    return z.fillna(0) > k

#Creating the final clean depth series
def clean_series(df: pd.DataFrame, k: float) -> pd.DataFrame:
    df = df.copy()
    df["is_outlier"] = detect_outliers(df["depth_raw"].ffill().bfill(), k=k)
    df["is_outlier"] = df["is_outlier"] | df["error_values"] #if value was detected as an outlier or it already had a error flag
    d = df["depth_raw"].where(~df["is_outlier"])
    df["depth_clean"] = d.interpolate(limit_direction="both")
    return df

# EMA and a simple scalar Kalman filter
def ema_filter(x: np.ndarray, alpha: float) -> np.ndarray:
    out = np.empty_like(x, dtype=float)
    out[0] = x[0]
    for i in range(1, len(x)):
        out[i] = alpha * x[i] + (1 - alpha) * out[i - 1]
    return out

def kalman_filter(x: np.ndarray, process_var: float, meas_var: float) -> np.ndarray:
    n = len(x)
    out = np.empty(n, dtype=float)
    est, p = x[0], 1.0
    for i in range(n):
        # predict
        p += process_var
        # update
        kgain = p / (p + meas_var)
        est = est + kgain * (x[i] - est)
        p *= (1 - kgain)
        out[i] = est
    return out

# Sidebar (CONTROL PANEL)
st.sidebar.title("Control Panel")
raw = load_raw("Depth_Data.csv")
n_total = len(raw)

st.sidebar.subheader("▶ Playback")
speed = st.sidebar.slider("Playback speed (×)", 1, 20, 1)
c1, c2, c3 = st.sidebar.columns(3)
play_clicked = c1.button("▶ Play")
pause_clicked = c2.button("⏸ Pause")
reset_clicked = c3.button("⟲ Reset")

st.sidebar.subheader("👁 Display")
show_raw = st.sidebar.checkbox("Show raw signal", True)
show_flags = st.sidebar.checkbox("Highlight corrupted/outlier points", True)
show_filtered = st.sidebar.checkbox("Show filtered signal", True)

st.sidebar.subheader("Outlier rejection")
mad_k = st.sidebar.slider("Sensitivity (lower = stricter)", 2.0, 8.0, 5.0, 0.5)

st.sidebar.subheader("Smoothing filter")
filter_type = st.sidebar.radio("Method", ["EMA", "Kalman", "None"], horizontal=True)
if filter_type == "EMA":
    alpha = st.sidebar.slider("EMA α (higher = more responsive, less smooth)", 0.02, 1.0, 0.25)
elif filter_type == "Kalman":
    q = st.sidebar.slider("Process noise (Q)", 0.001, 5.0, 0.5)
    r = st.sidebar.slider("Measurement noise (R)", 0.1, 50.0, 10.0)

st.sidebar.subheader("Grounding safety")
default_thresh = float(np.percentile(raw["depth_raw"].dropna(), 80))
threshold = st.sidebar.slider(
    "Alert threshold (m, shallower than this = danger)",
    float(raw["depth_raw"].min()), 0.0, default_thresh,
)

# Session state (drives the per-second animation)
if "idx" not in st.session_state:
    st.session_state.idx = 1
if "running" not in st.session_state:
    st.session_state.running = False

if play_clicked:
    st.session_state.running = True
if pause_clicked:
    st.session_state.running = False
if reset_clicked:
    st.session_state.idx = 1
    st.session_state.running = False

# Processes data up to current playhead
processed = clean_series(raw, k=mad_k)
idx = min(st.session_state.idx, n_total)
window = processed.iloc[:idx].copy()

clean_vals = window["depth_clean"].to_numpy()
if filter_type == "EMA":
    window["depth_filtered"] = ema_filter(clean_vals, alpha)
elif filter_type == "Kalman":
    window["depth_filtered"] = kalman_filter(clean_vals, q, r)
else:
    window["depth_filtered"] = clean_vals

current_depth = window["depth_filtered"].iloc[-1]
prev_depth = window["depth_filtered"].iloc[-2] if len(window) > 1 else current_depth
rate = current_depth - prev_depth
n_flagged = int(window["is_outlier"].sum())

if current_depth >= threshold:
    status, css = "SHALLOW WATER", "danger"
elif current_depth >= threshold * 1.15:
    status, css = "CAUTION", "warn"
else:
    status, css = "SAFE", "safe"

# Live metrics
st.title("Sea Floor Depth Monitor")
st.caption("My heart breaks for Odysseus, that seasoned veteran cursed by fate so long - far from his loved ones still, he suffers torments.")

m1, m2, m3, m4 = st.columns(4)
with m1:
    st.markdown(
        f'<div class="metric-box"><div>Current Depth</div>'
        f'<div class="big-number">{current_depth:.1f} m</div></div>',
        unsafe_allow_html=True,
    )
with m2:
    st.markdown(
        f'<div class="metric-box"><div>Rate of Change</div>'
        f'<div class="big-number">{rate:+.2f} m/s</div></div>',
        unsafe_allow_html=True,
    )
with m3:
    st.markdown(
        f'<div class="metric-box"><div>Status</div>'
        f'<div class="big-number {css}">{status}</div></div>',
        unsafe_allow_html=True,
    )
with m4:
    st.markdown(
        f'<div class="metric-box"><div>Readings Processed</div>'
        f'<div class="big-number">{idx} / {n_total}</div></div>',
        unsafe_allow_html=True,
    )

if current_depth >= threshold:
    st.image("https://media.tenor.com/TqTmECBaOJgAAAAM/panic-anime.gif", width=200)
    st.error(f"ALERT: depth {current_depth:.1f} m has crossed the {threshold:.1f} m safety threshold. Intervene now!")

st.progress(idx / n_total)

# Live animated chart
fig = go.Figure()

if show_raw:
    fig.add_trace(go.Scatter(
        x=window["t"], y=window["depth_raw"], mode="lines+markers",
        name="Raw signal", line=dict(color="rgba(150,150,150,0.5)", width=1),
        marker=dict(size=3),
    ))

if show_flags:
    flagged = window[window["is_outlier"]]
    fig.add_trace(go.Scatter(
        x=flagged["t"], y=flagged["depth_raw"].fillna(flagged["depth_clean"]),
        mode="markers", name="Corrupted / outlier",
        marker=dict(color="#ff4b4b", size=10, symbol="x"),
    ))

if show_filtered:
    fig.add_trace(go.Scatter(
        x=window["t"], y=window["depth_filtered"], mode="lines",
        name=f"Filtered ({filter_type})", line=dict(color="#00d97e", width=3),
    ))

fig.add_hline(y=threshold, line_dash="dash", line_color="#ff4b4b",
              annotation_text="Grounding threshold", annotation_position="bottom right")

fig.update_layout(
    xaxis_title="Time (s)", yaxis_title="Depth (m)",
    template="plotly_dark", height=520,
    margin=dict(l=10, r=10, t=30, b=10),
    legend=dict(orientation="h", yanchor="bottom", y=1.02, xanchor="right", x=1),
    xaxis=dict(range=[0, n_total]),
)
st.plotly_chart(fig, use_container_width=True)

st.caption(
    f"Flagged {n_flagged} corrupted/erratic readings so far "
    f"(includes non-numeric sensor errors, sudden spikes, and dropouts)."
)

# Animation Setup
if st.session_state.running and idx < n_total:
    time.sleep(1.0 / speed)
    st.session_state.idx += 1
    st.rerun()
elif st.session_state.running and idx >= n_total:
    st.session_state.running = False
    st.success("Your Journey is over Odysseus. You are back home.")
