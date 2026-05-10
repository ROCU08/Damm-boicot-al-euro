"""Genera juegos de prueba (CSV por día L-V) cruzando todas las fuentes.

Fuentes (XLSX en el mismo directorio):
  - Hackaton.xlsx
      · 'Detalle entrega'    : 1 fila = 1 línea de producto en un albarán.
      · 'Cabecera Transporte': agrupa albaranes por nº transporte / repartidor.
      · 'Direcciones'        : maestro de clientes (1 dirección por cliente).
      · 'ZONAS'              : mapeo cliente → zona entrega → ruta canónica.
      · 'Materiales zubic'   : ubicación física en almacén de cada material.
  - Horarios Entrega.XLSX    : ventanas horarias por cliente × día semana × turno.
  - ZM040.XLSX               : peso/volumen por (Material, UMA).

Salida (en el mismo directorio):
  - juego_lunes.csv ... juego_viernes.csv  (1 fila = 1 línea de albarán enriquecida)
  - geocode_cache.csv  (cache global de direcciones; reutilizado entre ejecuciones)

Esquema de cada juego (25 columnas):
  Dia, Fecha, Transporte, Ruta, Repartidor,
  ID_Pedido, Destino_ID, Cliente_Nombre,
  Material_ID, Material_Denom, Cantidad, UMA,
  Es_Barril, Peso_KG, Vol_M3,
  Ubic_Almacen, Zona_Entrega, RutReal,
  Calle, CP, Poblacion, Coordenadas,
  Hora_Inicio, Hora_Fin, Cierre
"""

import argparse
import csv
import sys
import time
import unicodedata
from datetime import date
from pathlib import Path

import pandas as pd

DAY_NAMES_ES = {0: "Lunes", 1: "Martes", 2: "Miercoles", 3: "Jueves", 4: "Viernes"}
WEEKDAY_ORDER = ["Lunes", "Martes", "Miercoles", "Jueves", "Viernes"]
GEOCODE_FLUSH_EVERY = 20
GEOCODE_DELAY_S = 1.0           # Photon es bastante laxo; 1 req/s razonable
GEOCODE_ERROR_WAIT_S = 10.0     # backoff tras error
GEOCODE_MAX_CONSEC_FAIL = 15    # abortamos geocoding (no el script) tras N fallos seguidos
# bbox aproximado de la Península Ibérica + Baleares para sesgar resultados
SPAIN_BBOX = [(35.0, -10.0), (44.0, 5.0)]  # (latmin, lonmin), (latmax, lonmax)

SCRIPT_DIR = Path(__file__).resolve().parent

OUTPUT_COLUMNS = [
    "Dia", "Fecha", "Transporte", "Ruta", "Repartidor",
    "ID_Pedido", "Destino_ID", "Cliente_Nombre",
    "Material_ID", "Material_Denom", "Cantidad", "UMA",
    "Es_Barril", "Peso_KG", "Vol_M3",
    "Ubic_Almacen", "Zona_Entrega", "RutReal",
    "Calle", "CP", "Poblacion", "Coordenadas",
    "Hora_Inicio", "Hora_Fin", "Cierre",
]


# ---------- carga ---------------------------------------------------------

def load_hackaton_sheets(path: Path) -> dict[str, pd.DataFrame]:
    sheets = pd.read_excel(path, sheet_name=None, engine="openpyxl", dtype=str)
    for name, df in sheets.items():
        df.columns = [c.strip() for c in df.columns]
        sheets[name] = df.fillna("")
    return sheets


def load_horarios(path: Path) -> pd.DataFrame:
    df = pd.read_excel(path, sheet_name=0, engine="openpyxl", dtype=str)
    df.columns = [c.strip() for c in df.columns]
    return df.fillna("")


def load_zm040(path: Path) -> dict[tuple[str, str], tuple[float, float]]:
    df = pd.read_excel(path, sheet_name=0, engine="openpyxl", dtype=str)
    df.columns = [c.strip() for c in df.columns]
    df = df.fillna("")
    df["Peso bruto"] = pd.to_numeric(df["Peso bruto"], errors="coerce").fillna(0)
    df["Volumen"] = pd.to_numeric(df["Volumen"], errors="coerce").fillna(0)
    lookup: dict[tuple[str, str], tuple[float, float]] = {}
    for _, r in df.iterrows():
        key = (r["Material"].strip(), r["UMA"].strip())
        peso, vol = float(r["Peso bruto"]), float(r["Volumen"])
        prev = lookup.get(key)
        if prev is None or (peso > 0 and prev[0] == 0):
            lookup[key] = (peso, vol)
    return lookup


# ---------- transformaciones ---------------------------------------------

def lookup_weight_volume(material, uma, cantidad, zm040):
    peso_u, vol_l = zm040.get((material, uma), (0.0, 0.0))
    if peso_u == 0 and vol_l == 0:
        peso_u, vol_l = zm040.get((material, "UN"), (0.0, 0.0))
    return cantidad * peso_u, cantidad * vol_l / 1000.0


def is_barril(uma: str, denom: str) -> bool:
    return uma.strip().upper() == "BRL" or "BARRIL" in denom.upper()


def pick_full_week(detalle: pd.DataFrame) -> dict[str, date]:
    df = detalle.copy()
    df["FECHA_DT"] = pd.to_datetime(df["FECHA"], format="%d/%m/%Y", errors="coerce")
    df = df.dropna(subset=["FECHA_DT"])
    df["weekday"] = df["FECHA_DT"].dt.weekday
    df["iso_year"] = df["FECHA_DT"].dt.isocalendar().year
    df["iso_week"] = df["FECHA_DT"].dt.isocalendar().week
    weekdays_only = df[df["weekday"].between(0, 4)]
    grouped = weekdays_only.groupby(["iso_year", "iso_week"])["weekday"].nunique()
    full = grouped[grouped == 5].sort_index()
    if full.empty:
        raise RuntimeError("No hay ninguna semana con líneas en los 5 días laborables.")
    iso_year, iso_week = full.index[-1]
    week_rows = weekdays_only[
        (weekdays_only["iso_year"] == iso_year) & (weekdays_only["iso_week"] == iso_week)
    ]
    return {
        DAY_NAMES_ES[wd]: week_rows[week_rows["weekday"] == wd]["FECHA_DT"].iloc[0].date()
        for wd in range(5)
    }


def normalize_address(calle: str, cp: str, poblacion: str) -> str:
    raw = "|".join([calle.strip(), cp.strip(), poblacion.strip()]).upper()
    nfkd = unicodedata.normalize("NFKD", raw)
    ascii_only = "".join(c for c in nfkd if not unicodedata.combining(c))
    return " ".join(ascii_only.split())


# ---------- geocoding cache ----------------------------------------------

def load_geocode_cache(path: Path) -> dict[str, tuple[str, str]]:
    cache: dict[str, tuple[str, str]] = {}
    if not path.exists():
        return cache
    with path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            cache[row["direccion_normalizada"]] = (row["lat"], row["lon"])
    return cache


def save_geocode_cache(path: Path, cache: dict[str, tuple[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(["direccion_normalizada", "lat", "lon"])
        for k, (lat, lon) in sorted(cache.items()):
            w.writerow([k, lat, lon])


_STREET_PREFIX_NORM = [
    ("CALLE ", "Carrer "),
    ("C/", "Carrer "),
    ("C\\", "Carrer "),
    ("AVENIDA ", "Avinguda "),
    ("AVDA ", "Avinguda "),
    ("AVDA. ", "Avinguda "),
    ("PLAZA ", "Plaça "),
    ("PZA ", "Plaça "),
    ("PG. ", "Passeig "),
    ("PASEO ", "Passeig "),
    ("CAMINO ", "Camí "),
    ("CARRETERA ", "Carretera "),
    ("CTRA. ", "Carretera "),
    ("CTRA ", "Carretera "),
    ("URBANIZACION ", "Urbanització "),
]


def _clean_street(calle: str) -> str:
    """Catalanitza prefijos comunes y limpia el sufijo S/N para Nominatim."""
    s = calle.strip()
    if not s:
        return s
    upper = s.upper()
    for src, dst in _STREET_PREFIX_NORM:
        if upper.startswith(src):
            s = dst + s[len(src):]
            break
    # Quitar variantes de "S/N" finales (Nominatim no las traga)
    for suffix in (" S/N", " SN", ", S/N", " s/n", " sn"):
        if s.upper().endswith(suffix.upper()):
            s = s[: -len(suffix)].rstrip(", ")
            break
    return s


def _try_geocode_photon(geocode_rl, queries: list[tuple[str, str]], log_prefix: str) -> tuple[str, str, str]:
    """Prueba una lista de queries free-form contra Photon. Devuelve (lat, lon, modo).
    modo ∈ {'street','city','fail','rate_limited'}.
    """
    from geopy.exc import GeocoderRateLimited, GeocoderTimedOut, GeocoderServiceError

    for kind, query in queries:
        try:
            loc = geocode_rl(query, timeout=15, bbox=SPAIN_BBOX)
            if loc:
                return f"{loc.latitude:.6f}", f"{loc.longitude:.6f}", kind
        except GeocoderRateLimited:
            return "", "", "rate_limited"
        except (GeocoderTimedOut, GeocoderServiceError) as e:
            print(f"  {log_prefix} {type(e).__name__} en {kind}")
        except Exception as e:
            print(f"  {log_prefix} error '{type(e).__name__}' en {kind}")
    return "", "", "fail"


def geocode_addresses(new_addrs, cache, cache_path):
    """Geocodifica usando Photon (komoot) con cascada calle → centroide municipal.
    Termina siempre, aunque la API falle. Los rate-limited NO se cachean (se reintentan).
    """
    if not new_addrs:
        return
    import logging

    from geopy.geocoders import Photon
    from geopy.extra.rate_limiter import RateLimiter

    logging.getLogger("geopy").setLevel(logging.ERROR)

    geo = Photon(user_agent="damm_logistics_testgen")
    geocode_rl = RateLimiter(
        geo.geocode,
        min_delay_seconds=GEOCODE_DELAY_S,
        error_wait_seconds=GEOCODE_ERROR_WAIT_S,
        max_retries=2,
        swallow_exceptions=False,
    )

    print(f"Geocodificando hasta {len(new_addrs)} direcciones nuevas con Photon (delay={GEOCODE_DELAY_S}s)...")
    counts = {"street": 0, "city": 0, "fail": 0, "rate_limited": 0}
    flushed = 0
    consec_rate_limited = 0
    aborted = False

    for i, (key, calle, cp, poblacion) in enumerate(new_addrs, 1):
        if aborted:
            break
        calle_clean = _clean_street(calle.split(",")[0])
        queries: list[tuple[str, str]] = []
        if calle_clean:
            queries.append(("street", f"{calle_clean}, {cp} {poblacion}, Spain"))
            queries.append(("street", f"{calle_clean}, {poblacion}, Spain"))
        queries.append(("city", f"{cp} {poblacion}, Spain"))
        queries.append(("city", f"{poblacion}, Spain"))

        lat_str, lon_str, mode = _try_geocode_photon(geocode_rl, queries, f"[{i}/{len(new_addrs)}]")
        counts[mode] += 1

        if mode == "rate_limited":
            consec_rate_limited += 1
            print(f"  [{i}/{len(new_addrs)}] rate limit (consec {consec_rate_limited}/{GEOCODE_MAX_CONSEC_FAIL})")
            if consec_rate_limited >= GEOCODE_MAX_CONSEC_FAIL:
                print(f"  >>> Demasiados rate-limited seguidos. Aborto geocoding y escribo CSVs con lo que tengo.")
                aborted = True
            continue
        else:
            consec_rate_limited = 0

        if mode == "fail":
            print(f"  [{i}/{len(new_addrs)}] sin coords: {calle} | {cp} {poblacion}")
        elif mode == "city":
            print(f"  [{i}/{len(new_addrs)}] solo municipio: {cp} {poblacion}")

        cache[key] = (lat_str, lon_str)
        flushed += 1
        if flushed >= GEOCODE_FLUSH_EVERY:
            save_geocode_cache(cache_path, cache)
            flushed = 0

    save_geocode_cache(cache_path, cache)
    print(f"  Resumen: {counts['street']} street, {counts['city']} solo municipio, "
          f"{counts['fail']} sin coords, {counts['rate_limited']} rate-limited.")


# ---------- construcción del juego ---------------------------------------

def build_lookups(sheets: dict[str, pd.DataFrame]) -> dict:
    direcciones = sheets["Direcciones"].copy()
    direcciones["Cliente"] = direcciones["Cliente"].astype(str).str.strip()
    direcciones = direcciones.drop_duplicates(subset=["Cliente"], keep="first")
    direc_by_cliente = direcciones.set_index("Cliente").to_dict("index")

    zonas = sheets["ZONAS"].copy()
    zonas["cliente zona"] = zonas["cliente zona"].astype(str).str.strip()
    # cada cliente puede aparecer 1 vez en ZONAS; guardamos zona y RutReal
    zona_by_cliente: dict[str, dict[str, str]] = {}
    for _, r in zonas.iterrows():
        cli = r["cliente zona"]
        if not cli or cli in zona_by_cliente:
            continue
        zona_by_cliente[cli] = {
            "Zona_Entrega": r.get("Zona Entrega", ""),
            "RutReal": r.get("RutReal", ""),
        }

    matzubic = sheets["Materiales zubic"].copy()
    matzubic["Material"] = matzubic["Material"].astype(str).str.strip()
    ubic_by_material: dict[str, str] = {}
    for _, r in matzubic.iterrows():
        m = r["Material"]
        if m and m not in ubic_by_material:
            ubic_by_material[m] = r.get("Ubic.", "")

    return {
        "direc": direc_by_cliente,
        "zona": zona_by_cliente,
        "ubic": ubic_by_material,
    }


def build_horarios_lookup(horarios: pd.DataFrame) -> dict[tuple[str, int], dict[str, str]]:
    """Indexa horarios por (Deudor, Día semana). Si hay varios turnos coge el primero (Turno=1)."""
    out: dict[tuple[str, int], dict[str, str]] = {}
    for _, r in horarios.iterrows():
        try:
            dia = int(r["Día semana"])
        except (ValueError, TypeError):
            continue
        deudor = str(r["Deudor"]).strip()
        turno = str(r.get("Turno", "")).strip()
        key = (deudor, dia)
        # preferir Turno=1 si ya hay otro registrado
        if key in out and turno != "1":
            continue
        out[key] = {
            "Hora_Inicio": str(r.get("Horario inicia a", "")).strip(),
            "Hora_Fin": str(r.get("Horario termina a", "")).strip(),
            "Cierre": str(r.get("Cierre Si/No", "")).strip(),
        }
    return out


def build_day_rows(
    day_name: str,
    detalle_dia: pd.DataFrame,
    zm040,
    lookups,
    horarios_idx,
    cache,
) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    cliente_col = "Destinatario mcía..1"
    weekday_num = WEEKDAY_ORDER.index(day_name) + 1  # 1..5

    for _, r in detalle_dia.iterrows():
        cliente = str(r[cliente_col]).strip()
        material = str(r["Material"]).strip()
        uma = str(r["Un.medida venta"]).strip()
        denom = str(r["Denominación"]).strip()
        try:
            cantidad = float(r["Cantidad entrega"])
        except (ValueError, TypeError):
            cantidad = 0.0

        peso, vol = lookup_weight_volume(material, uma, cantidad, zm040)

        # Dirección: preferir maestro Direcciones (más limpio), si no, lo de la propia línea
        direc = lookups["direc"].get(cliente)
        if direc:
            calle = str(direc.get("Calle", "")).strip()
            cp = str(direc.get("CP", "")).strip()
            poblacion = str(direc.get("Población", "")).strip()
            cliente_nombre = str(direc.get("Nombre 1", "")).strip()
        else:
            calle = str(r["Calle"]).strip()
            cp = str(r["CP"]).strip()
            poblacion = str(r["Población"]).strip()
            cliente_nombre = str(r["Nombre 1"]).strip()

        addr_key = normalize_address(calle, cp, poblacion)
        lat, lon = cache.get(addr_key, ("", ""))
        coords = f"{lat},{lon}" if lat and lon else ""

        zona_info = lookups["zona"].get(cliente, {})
        ubic = lookups["ubic"].get(material, "")
        hor = horarios_idx.get((cliente, weekday_num), {})

        rows.append({
            "Dia": day_name,
            "Fecha": str(r["FECHA"]).strip(),
            "Transporte": str(r["Transporte"]).strip(),
            "Ruta": str(r["Ruta"]).strip(),
            "Repartidor": str(r["Repartidor"]).strip(),
            "ID_Pedido": str(r["Entrega"]).strip(),
            "Destino_ID": cliente,
            "Cliente_Nombre": cliente_nombre,
            "Material_ID": material,
            "Material_Denom": denom,
            "Cantidad": str(r["Cantidad entrega"]).strip(),
            "UMA": uma,
            "Es_Barril": "1" if is_barril(uma, denom) else "0",
            "Peso_KG": f"{peso:.3f}",
            "Vol_M3": f"{vol:.6f}",
            "Ubic_Almacen": ubic,
            "Zona_Entrega": zona_info.get("Zona_Entrega", ""),
            "RutReal": zona_info.get("RutReal", ""),
            "Calle": calle,
            "CP": cp,
            "Poblacion": poblacion,
            "Coordenadas": coords,
            "Hora_Inicio": hor.get("Hora_Inicio", ""),
            "Hora_Fin": hor.get("Hora_Fin", ""),
            "Cierre": hor.get("Cierre", ""),
        })
    return rows


def aggregate_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    """Agrupa por (ID_Pedido, Material_ID, UMA) y suma Cantidad/Peso_KG/Vol_M3.

    El XLSX puede traer la misma combinación en líneas separadas (lote, precio, promo).
    Para los juegos de prueba esa info no se conserva, así que las colapsamos.
    """
    grouped: dict[tuple[str, str, str], dict[str, str]] = {}
    sums: dict[tuple[str, str, str], list[float]] = {}  # [cantidad, peso, vol]
    for r in rows:
        key = (r["ID_Pedido"], r["Material_ID"], r["UMA"])
        try:
            cant = float(r["Cantidad"]) if r["Cantidad"] else 0.0
        except ValueError:
            cant = 0.0
        peso = float(r["Peso_KG"]) if r["Peso_KG"] else 0.0
        vol = float(r["Vol_M3"]) if r["Vol_M3"] else 0.0
        if key not in grouped:
            grouped[key] = dict(r)
            sums[key] = [cant, peso, vol]
        else:
            sums[key][0] += cant
            sums[key][1] += peso
            sums[key][2] += vol
    out: list[dict[str, str]] = []
    for key, g in grouped.items():
        cant_t, peso_t, vol_t = sums[key]
        g["Cantidad"] = str(int(cant_t)) if cant_t.is_integer() else f"{cant_t:.3f}"
        g["Peso_KG"] = f"{peso_t:.3f}"
        g["Vol_M3"] = f"{vol_t:.6f}"
        out.append(g)
    return out


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=OUTPUT_COLUMNS)
        w.writeheader()
        w.writerows(rows)


# ---------- main ---------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=SCRIPT_DIR)
    parser.add_argument("--out-dir", type=Path, default=SCRIPT_DIR)
    parser.add_argument("--no-geocode", action="store_true",
                        help="No llama a Nominatim; deja Coordenadas vacías para direcciones no cacheadas.")
    args = parser.parse_args()

    hackaton_path = args.data_dir / "Hackaton.xlsx"
    horarios_path = args.data_dir / "Horarios Entrega.XLSX"
    zm040_path = args.data_dir / "ZM040.XLSX"
    cache_path = args.out_dir / "geocode_cache.csv"

    print(f"Cargando {hackaton_path.name} (5 hojas)...")
    sheets = load_hackaton_sheets(hackaton_path)
    detalle = sheets["Detalle entrega"]
    print(f"  Detalle entrega: {len(detalle)} líneas")
    print(f"  Cabecera Transporte: {len(sheets['Cabecera Transporte'])}")
    print(f"  Direcciones: {len(sheets['Direcciones'])}")
    print(f"  ZONAS: {len(sheets['ZONAS'])}")
    print(f"  Materiales zubic: {len(sheets['Materiales zubic'])}")

    print(f"Cargando {horarios_path.name}...")
    horarios = load_horarios(horarios_path)
    print(f"  {len(horarios)} ventanas horarias")
    horarios_idx = build_horarios_lookup(horarios)

    print(f"Cargando {zm040_path.name}...")
    zm040 = load_zm040(zm040_path)
    print(f"  {len(zm040)} entradas (Material, UMA)")

    print("Detectando semana completa más reciente...")
    week = pick_full_week(detalle)
    for d, fecha in week.items():
        print(f"  {d}: {fecha.isoformat()}")

    lookups = build_lookups(sheets)
    cache = load_geocode_cache(cache_path)
    print(f"Caché de geocoding: {len(cache)} entradas existentes")

    # parsear FECHA en detalle una sola vez
    detalle = detalle.copy()
    detalle["FECHA_DT"] = pd.to_datetime(detalle["FECHA"], format="%d/%m/%Y", errors="coerce")

    # Identificar TODAS las direcciones únicas necesarias para la semana objetivo
    needed_addrs: dict[str, tuple[str, str, str]] = {}
    day_dfs: dict[str, pd.DataFrame] = {}
    for d, fecha in week.items():
        sub = detalle[detalle["FECHA_DT"].dt.date == fecha]
        day_dfs[d] = sub
        cliente_col = "Destinatario mcía..1"
        for _, r in sub.iterrows():
            cliente = str(r[cliente_col]).strip()
            direc = lookups["direc"].get(cliente)
            if direc:
                calle = str(direc.get("Calle", "")).strip()
                cp = str(direc.get("CP", "")).strip()
                poblacion = str(direc.get("Población", "")).strip()
            else:
                calle = str(r["Calle"]).strip()
                cp = str(r["CP"]).strip()
                poblacion = str(r["Población"]).strip()
            key = normalize_address(calle, cp, poblacion)
            if not key or key in needed_addrs:
                continue
            cached = cache.get(key)
            # Reintenta si no hay entrada o si la entrada cacheada está vacía (fallo previo).
            if cached is None or not cached[0]:
                needed_addrs[key] = (calle, cp, poblacion)

    if args.no_geocode:
        print(f"--no-geocode activo: {len(needed_addrs)} direcciones quedan sin coordenadas")
    else:
        new_list = [(k, c, cp, p) for k, (c, cp, p) in needed_addrs.items()]
        geocode_addresses(new_list, cache, cache_path)

    for d in WEEKDAY_ORDER:
        sub = day_dfs[d]
        raw_rows = build_day_rows(d, sub, zm040, lookups, horarios_idx, cache)
        rows = aggregate_rows(raw_rows)
        out_path = args.out_dir / f"juego_{d.lower()}.csv"
        write_csv(out_path, rows)
        with_coords = sum(1 for r in rows if r["Coordenadas"])
        with_window = sum(1 for r in rows if r["Hora_Inicio"])
        with_zona = sum(1 for r in rows if r["Zona_Entrega"])
        print(f"  {out_path.name}: {len(rows)} filas (de {len(raw_rows)} brutas) | "
              f"coords {with_coords} ({with_coords/max(len(rows),1):.0%}) | "
              f"horarios {with_window} ({with_window/max(len(rows),1):.0%}) | "
              f"zona {with_zona} ({with_zona/max(len(rows),1):.0%})")

    return 0


if __name__ == "__main__":
    sys.exit(main())
