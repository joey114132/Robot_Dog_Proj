import serial, time, json, os, random, re, requests
import sys

PORT = "/dev/ttyACM0"
BAUD = 115200

OLLAMA_URL = "http://localhost:11434/api/generate"
MODEL = "llama3.1"

SAVE_FILE = "dogbot_state.json"
MAX_SEND_CHARS = 160

DOG_ENDINGS = ["woof!", "ruff!", "arf!", "woo!", "bork!"]

EMO_HAPPY, EMO_NEUTRAL, EMO_SAD, EMO_SLEEP = 0, 1, 2, 3

last_ollama_call = 0
MIN_OLLAMA_GAP = 2.0

ENABLE_OLLAMA = True  # Always use AI

# Neutral action descriptions - these only tell AI *what happened*, not *how to feel*
# The AI decides the emotion entirely based on bond/mood in the prompt
ACTION_CONTEXTS = {
    "PET": "being gently petted on the head and body",
    "THROW": "chasing after a thrown ball and fetching it",
    "SCRATCH": "getting ear and belly scratches",
    "SIT": "obediently sitting on command",
    "TREAT": "receiving and eating a tasty treat",
    "RAND": "spontaneously doing a cute random dog behavior",
    "STATUS": "being asked for a status update about feelings and bond",
    "IDLE10": "being ignored for a long time with no interaction",
}

def load_state():
    s = {
        "name": "DogBot",
        "bond": 50,
        "emotion": EMO_NEUTRAL,
        "sleep": False,
        "last_msg": "Hi master! woof!",
    }
    if os.path.exists(SAVE_FILE):
        try:
            with open(SAVE_FILE, "r", encoding="utf-8") as f:
                s.update(json.load(f))
        except:
            pass
    s["bond"] = int(max(0, min(100, s.get("bond", 50))))
    s["emotion"] = int(max(0, min(3, s.get("emotion", EMO_NEUTRAL))))
    s["sleep"] = bool(s.get("sleep", False))
    s["last_msg"] = str(s.get("last_msg", "")) or "Hi! woof!"
    return s

def save_state(s):
    with open(SAVE_FILE, "w", encoding="utf-8") as f:
        json.dump(s, f, ensure_ascii=False, indent=2)

def to_lcd_safe_ascii(t: str) -> str:
    t = t.replace("'", "'").replace("'", "'").replace(""", '"').replace(""", '"')
    t = t.replace("–", "-").replace("—", "-").replace("…", "...")
    t = t.encode("ascii", errors="ignore").decode("ascii")
    t = re.sub(r"\s+", " ", t).strip()
    return t

def sanitize(text: str) -> str:
    t = (text or "").replace("\n", " ").strip()
    t = re.sub(r'^\s*\[(user|assistant|system|ai)\]\s*', '', t, flags=re.I)
    t = re.sub(r'^\s*(ai\s*)?(assistant|bot|model|chatgpt|ai)\s*:\s*', '', t, flags=re.I)
    t = re.sub(r'\[(user|assistant|system|ai)\]\s*', '', t, flags=re.I)
    t = re.split(r'(?:###|---|<\|end\|>)', t, maxsplit=1)[0].strip()
    t = re.sub(r'\s+', ' ', t).strip()
    return t.strip(" \"'")

def truncate_word_boundary(text: str, max_chars: int) -> str:
    t = text.strip()
    if len(t) <= max_chars:
        return t
    cut = t[:max_chars]
    sp = cut.rfind(" ")
    if sp >= 30:
        cut = cut[:sp]
    return cut.strip()

def maybe_add_ending(text: str) -> str:
    t = text.strip().rstrip("!.?")
    if not t:
        return random.choice(DOG_ENDINGS)
    # Only add if no dog sound already at end
    if not any(t.lower().endswith(ending.rstrip("!").lower()) for ending in DOG_ENDINGS):
        t += " " + random.choice(DOG_ENDINGS)
    else:
        t += random.choice(["!", "."])
    return t

def finalize_for_lcd(text: str) -> str:
    t = to_lcd_safe_ascii(text)
    t = sanitize(t)
    t = maybe_add_ending(t)
    t = truncate_word_boundary(t, MAX_SEND_CHARS)
    return t or random.choice(DOG_ENDINGS)

def ollama(prompt: str) -> str:
    global last_ollama_call
    
    now = time.time()
    time_since_last = now - last_ollama_call
    if time_since_last < MIN_OLLAMA_GAP:
        time.sleep(MIN_OLLAMA_GAP - time_since_last)
    
    try:
        print(f"[OLLAMA] Calling API...")
        r = requests.post(
            OLLAMA_URL,
            json={"model": MODEL, "prompt": prompt, "stream": False},
            timeout=30,
        )
        last_ollama_call = time.time()
        response = (r.json().get("response") or "").strip()
        print(f"[OLLAMA] Response: {response[:60]}...")
        return response
    except Exception as e:
        print(f"[OLLAMA ERROR] {e}")
        return ""

def send_stat(ser, s):
    msg = f"STAT:{s['bond']}|{s['emotion']}|{s['name']}\n"
    print(f"[SEND] {msg.strip()}")
    ser.write(msg.encode("utf-8"))
    ser.flush()

def send_msg(ser, text):
    msg = finalize_for_lcd(text)
    wire_msg = f"MSG:{msg}\n"
    print(f"[SEND] MSG: {msg}")
    ser.write(wire_msg.encode("utf-8"))
    ser.flush()

def set_emotion(s):
    if s["sleep"]:
        s["emotion"] = EMO_SLEEP
        return
    b = s["bond"]
    if b >= 70:
        s["emotion"] = EMO_HAPPY
    elif b >= 35:
        s["emotion"] = EMO_NEUTRAL
    else:
        s["emotion"] = EMO_SAD

def bond_change(s, delta):
    old_bond = s["bond"]
    s["bond"] = max(0, min(100, s["bond"] + delta))
    print(f"[BOND] {old_bond} -> {s['bond']} ({delta:+d})")

def prompt_translate_feelings(s, action_context: str) -> str:
    sleep_line = "DogBot is sleepy and slow." if s["sleep"] else "DogBot is awake and alert."
    bond_level = s['bond']
    
    if bond_level >= 70:
        mood_desc = "extremely happy, excited, playful, and deeply bonded"
    elif bond_level >= 50:
        mood_desc = "content, friendly, and affectionate"
    elif bond_level >= 35:
        mood_desc = "a bit sad and needing more attention"
    elif bond_level >= 20:
        mood_desc = "quite lonely and craving interaction"
    else:
        mood_desc = "very sad, neglected, and desperate for love"
    
    return (
        "You are DogBot, a robot dog with real emotions. "
        "Respond with exactly ONE short, heartfelt sentence in first person as the dog. "
        "Express genuine feelings naturally like a real dog would. "
        "Use simple words and include dog sounds like woof, ruff, arf, bork naturally when it fits. "
        "Never use quotes, labels, brackets, or explanations. "
        "Never respond with empty text. "
        "Make it fit on a tiny LCD screen. "
        f"{sleep_line} Your current bond with the human is {bond_level}/100 - you feel {mood_desc}. "
        f"You are currently {action_context}. "
        "Respond directly with your emotional reaction to this."
    )

def main():
    print("=" * 60)
    print("DOGBOT PYTHON CONTROLLER - 100% AI RESPONSES")
    print("=" * 60)
    
    s = load_state()
    
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        print(f"[SERIAL] Connected")
    except Exception as e:
        print(f"[ERROR] Serial: {e}")
        sys.exit(1)
    
    time.sleep(2)
    set_emotion(s)
    send_stat(ser, s)
    send_msg(ser, s["last_msg"])

    while True:
        if ser.in_waiting <= 0:
            time.sleep(0.02)
            continue

        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if not line:
            continue
        
        if line.startswith("DEBUG:"):
            print(f"[ARDUINO] {line}")
            continue
        
        print(f"[RECV] {line}")

        if line == "HELLO":
            set_emotion(s)
            send_stat(ser, s)
            send_msg(ser, s["last_msg"])
            continue

        if line.startswith("STATE:"):
            st = line.split(":", 1)[1].strip().upper()
            s["sleep"] = (st == "SLEEP")
            set_emotion(s)
            save_state(s)
            send_stat(ser, s)

            action_context = "falling asleep" if s["sleep"] else "waking up excitedly"
            out = ollama(prompt_translate_feelings(s, action_context))
            msg = finalize_for_lcd(out)
            s["last_msg"] = msg
            save_state(s)
            send_msg(ser, msg)
            continue

        if line.startswith("EVENT:"):
            ev = line.split(":", 1)[1].strip().upper()
            print(f"[EVENT] {ev}")

            # Bond changes (mechanics stay scripted - only speech is AI)
            if ev == "PET":
                bond_change(s, +3 if not s["sleep"] else +1)
            elif ev == "THROW":
                bond_change(s, +2 if not s["sleep"] else -1)
            elif ev == "SCRATCH":
                bond_change(s, +2 if not s["sleep"] else +1)
            elif ev == "SIT":
                bond_change(s, +1 if not s["sleep"] else 0)
            elif ev == "TREAT":
                bond_change(s, +4 if not s["sleep"] else +2)
            elif ev == "IDLE10":
                bond_change(s, -1 if s["sleep"] else -2)

            set_emotion(s)
            save_state(s)
            send_stat(ser, s)

            # Pure neutral action description - AI adds all emotion
            action_context = ACTION_CONTEXTS.get(ev, "a positive interaction with the human")

            # Special for idle (still neutral, AI will make sad if low bond)
            if ev == "IDLE10":
                action_context = "being ignored for a long time"

            out = ollama(prompt_translate_feelings(s, action_context))
            msg = finalize_for_lcd(out)
            s["last_msg"] = msg
            save_state(s)
            send_msg(ser, msg)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[EXIT] Shutting down...")
        sys.exit(0)