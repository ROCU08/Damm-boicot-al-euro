// Paleta categórica estable: cada camion_id se mapea a un color fijo.
// Inspirado en `tab10` de matplotlib para coherencia con los plots de viz/.
const PALETTE = [
  "#1f77b4", // azul
  "#ff7f0e", // naranja
  "#2ca02c", // verde
  "#d62728", // rojo
  "#9467bd", // violeta
  "#8c564b", // marrón
  "#e377c2", // rosa
  "#7f7f7f", // gris
  "#bcbd22", // amarillo-verdoso
  "#17becf", // cian
];

export function colorCamion(camionId: number): string {
  return PALETTE[((camionId % PALETTE.length) + PALETTE.length) % PALETTE.length];
}

// Color por cliente: una variante más clara, basada en clienteId.
// Útil para teñir items en la vista 3D usando el mismo color que en el mapa.
const PALETTE_CLIENTE = [
  "#a6cee3", "#fdbf6f", "#b2df8a", "#fb9a99",
  "#cab2d6", "#ffff99", "#fccde5", "#bdbdbd",
  "#dde26a", "#a6d8e0",
  "#1f78b4", "#ff7f00", "#33a02c", "#e31a1c",
  "#6a3d9a", "#b15928",
];

export function colorCliente(clienteId: number): string {
  return PALETTE_CLIENTE[((clienteId % PALETTE_CLIENTE.length) + PALETTE_CLIENTE.length) % PALETTE_CLIENTE.length];
}
