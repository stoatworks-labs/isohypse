// GENERATED from source/WorldTable.inc by demo/tools/make_worlds.py. Do not edit:
// tools/verify.sh fails if it is not what that script would write.
//
// The plugin's table, in its order (the World and Place dropdowns are
// append-only, so the order is the index a composition stores). width and
// height are the plugin's file; servedWidth and servedHeight are the file
// this page fetches from ./worlds/, half the size for a globe.

export const GLOBES = [
  { name: 'Earth', file: 'earth.png', kind: 'globe', width: 4096, height: 2048, servedWidth: 2048, servedHeight: 1024, minMetres: -10359.1, maxMetres: 6366.7, hasNodata: false, hasSea: true },
  { name: 'Moon', file: 'moon.png', kind: 'globe', width: 2048, height: 1024, servedWidth: 1024, servedHeight: 512, minMetres: -8697.7, maxMetres: 10513.1, hasNodata: false, hasSea: false },
  { name: 'Mars', file: 'mars.png', kind: 'globe', width: 2048, height: 1024, servedWidth: 1024, servedHeight: 512, minMetres: -8121.6, maxMetres: 21141.8, hasNodata: false, hasSea: false },
  { name: 'Mercury', file: 'mercury.png', kind: 'globe', width: 2048, height: 1024, servedWidth: 1024, servedHeight: 512, minMetres: -5357.4, maxMetres: 4434.6, hasNodata: false, hasSea: false },
  { name: 'Venus', file: 'venus.png', kind: 'globe', width: 2048, height: 1024, servedWidth: 1024, servedHeight: 512, minMetres: -2885.5, maxMetres: 11367.0, hasNodata: true, hasSea: false },
  { name: 'Ceres', file: 'ceres.png', kind: 'globe', width: 2048, height: 1024, servedWidth: 1024, servedHeight: 512, minMetres: -7240.3, maxMetres: 9430.7, hasNodata: false, hasSea: false },
  { name: 'Vesta', file: 'vesta.png', kind: 'globe', width: 2048, height: 1024, servedWidth: 1024, servedHeight: 512, minMetres: -22388.9, maxMetres: 19581.2, hasNodata: false, hasSea: false },
  { name: 'Pluto', file: 'pluto.png', kind: 'globe', width: 2048, height: 1024, servedWidth: 1024, servedHeight: 512, minMetres: -4036.5, maxMetres: 6072.8, hasNodata: true, hasSea: false },
  { name: 'Charon', file: 'charon.png', kind: 'globe', width: 2048, height: 1024, servedWidth: 1024, servedHeight: 512, minMetres: -14028.5, maxMetres: 6864.4, hasNodata: true, hasSea: false },
];

export const PLACES = [
  { name: 'London', file: 'london.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -4.7, maxMetres: 134.8, hasNodata: false, hasSea: false, centreLat: 51.5072, centreLon: -0.1276, metresPerPixel: 47.58 },
  { name: 'Edinburgh', file: 'edinburgh.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -11.5, maxMetres: 224.4, hasNodata: false, hasSea: true, centreLat: 55.9533, centreLon: -3.1883, metresPerPixel: 21.40 },
  { name: 'Snowdon', file: 'snowdon.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: 43.7, maxMetres: 1044.6, hasNodata: false, hasSea: false, centreLat: 53.0685, centreLon: -4.0763, metresPerPixel: 22.96 },
  { name: 'New York', file: 'new-york.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -37.4, maxMetres: 123.7, hasNodata: false, hasSea: true, centreLat: 40.7128, centreLon: -74.0060, metresPerPixel: 57.94 },
  { name: 'San Francisco', file: 'san-francisco.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -114.0, maxMetres: 463.9, hasNodata: false, hasSea: true, centreLat: 37.7749, centreLon: -122.4194, metresPerPixel: 60.42 },
  { name: 'Los Angeles', file: 'los-angeles.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -26.7, maxMetres: 2433.5, hasNodata: false, hasSea: true, centreLat: 34.0522, centreLon: -118.2437, metresPerPixel: 126.66 },
  { name: 'Vancouver', file: 'vancouver.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -9.0, maxMetres: 1755.4, hasNodata: false, hasSea: true, centreLat: 49.2827, centreLon: -123.1207, metresPerPixel: 99.72 },
  { name: 'Mount Rainier', file: 'mount-rainier.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: 582.5, maxMetres: 4386.5, hasNodata: false, hasSea: false, centreLat: 46.8523, centreLon: -121.7603, metresPerPixel: 52.27 },
  { name: 'Grand Canyon', file: 'grand-canyon.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: 687.3, maxMetres: 2639.5, hasNodata: false, hasSea: false, centreLat: 36.1069, centreLon: -112.1129, metresPerPixel: 61.75 },
  { name: 'Crater Lake', file: 'crater-lake.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: 1642.2, maxMetres: 2588.4, hasNodata: false, hasSea: false, centreLat: 42.9446, centreLon: -122.1090, metresPerPixel: 27.98 },
  { name: 'Rio de Janeiro', file: 'rio-de-janeiro.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -61.8, maxMetres: 980.0, hasNodata: false, hasSea: true, centreLat: -22.9068, centreLon: -43.1729, metresPerPixel: 70.41 },
  { name: 'Cape Town', file: 'cape-town.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -28.7, maxMetres: 1079.3, hasNodata: false, hasSea: true, centreLat: -33.9249, centreLon: 18.4241, metresPerPixel: 63.43 },
  { name: 'Lisbon', file: 'lisbon.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -4.5, maxMetres: 215.9, hasNodata: false, hasSea: true, centreLat: 38.7223, centreLon: -9.1393, metresPerPixel: 29.82 },
  { name: 'Naples & Vesuvius', file: 'naples-vesuvius.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -4.0, maxMetres: 1255.9, hasNodata: false, hasSea: true, centreLat: 40.8300, centreLon: 14.3500, metresPerPixel: 57.84 },
  { name: 'Matterhorn', file: 'matterhorn.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: 1616.3, maxMetres: 4353.4, hasNodata: false, hasSea: false, centreLat: 45.9763, centreLon: 7.6586, metresPerPixel: 26.56 },
  { name: 'Everest', file: 'everest.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: 3418.5, maxMetres: 8743.5, hasNodata: false, hasSea: false, centreLat: 27.9881, centreLon: 86.9250, metresPerPixel: 67.50 },
  { name: 'Kilimanjaro', file: 'kilimanjaro.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: 1204.2, maxMetres: 5880.5, hasNodata: false, hasSea: false, centreLat: -3.0674, centreLon: 37.3556, metresPerPixel: 76.33 },
  { name: 'Mount Fuji', file: 'mount-fuji.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: 105.7, maxMetres: 3744.3, hasNodata: false, hasSea: false, centreLat: 35.3606, centreLon: 138.7274, metresPerPixel: 62.34 },
  { name: 'Hong Kong', file: 'hong-kong.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -46.8, maxMetres: 947.8, hasNodata: false, hasSea: true, centreLat: 22.3193, centreLon: 114.1694, metresPerPixel: 70.71 },
  { name: 'Wellington', file: 'wellington.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -29.4, maxMetres: 783.5, hasNodata: false, hasSea: true, centreLat: -41.2865, centreLon: 174.7762, metresPerPixel: 57.44 },
  { name: 'Sydney', file: 'sydney.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -60.0, maxMetres: 317.3, hasNodata: false, hasSea: true, centreLat: -33.8688, centreLon: 151.2093, metresPerPixel: 126.93 },
  { name: 'Hawaii (Big Island)', file: 'hawaii-big-island.png', kind: 'place', width: 512, height: 512, servedWidth: 512, servedHeight: 512, minMetres: -5877.3, maxMetres: 4155.3, hasNodata: false, hasSea: true, centreLat: 19.6000, centreLon: -155.5000, metresPerPixel: 576.06 },
];
