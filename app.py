from flask import Flask, request, jsonify, render_template
import sqlite3, time, os

app = Flask(__name__)
DB = "/app/data/countify.db"

def get_db():
    con = sqlite3.connect(DB)
    con.row_factory = sqlite3.Row
    return con

def init_db():
    with get_db() as con:
        con.execute("""
            CREATE TABLE IF NOT EXISTS verlauf (
                id        INTEGER PRIMARY KEY AUTOINCREMENT,
                zeitpunkt INTEGER NOT NULL,
                personen  INTEGER NOT NULL,
                sensor1   INTEGER,
                sensor2   INTEGER
            )
        """)

@app.route('/update', methods=['GET', 'POST'])
def update():
    if request.method == 'POST':
        d = request.get_json()
        if d is None:
            return jsonify({"error": "kein JSON"}), 400
        with get_db() as con:
            con.execute(
                "INSERT INTO verlauf (zeitpunkt, personen, sensor1, sensor2) VALUES (?,?,?,?)",
                (int(time.time()), d.get("personen", 0),
                 d.get("sensor1", 0), d.get("sensor2", 0))
            )
        return jsonify({"status": "ok"})
    else:
        with get_db() as con:
            row = con.execute(
                "SELECT * FROM verlauf ORDER BY zeitpunkt DESC LIMIT 1"
            ).fetchone()
        if row is None:
            return jsonify({"personen": 0, "sensor1": 0, "sensor2": 0})
        return jsonify(dict(row))

@app.route("/api/live")
def live():
    with get_db() as con:
        row = con.execute(
            "SELECT * FROM verlauf ORDER BY zeitpunkt DESC LIMIT 1"
        ).fetchone()
    if row is None:
        return jsonify({"personen": 0, "sensor1": 0, "sensor2": 0})
    return jsonify(dict(row))

@app.route("/api/verlauf")
def verlauf():
    seit = int(time.time()) - 3600
    with get_db() as con:
        rows = con.execute(
            "SELECT zeitpunkt, personen FROM verlauf WHERE zeitpunkt > ? ORDER BY zeitpunkt ASC",
            (seit,)
        ).fetchall()
    return jsonify([dict(r) for r in rows])

@app.route("/")
def index():
    return render_template("index.html")

if __name__ == "__main__":
    init_db()
    app.run(host="0.0.0.0", port=5000, debug=False)