// Source: https://stackoverflow.com/questions/11042212/ff-13-ie-9-json-stringify-geolocation-object
const cloneAsObject = (obj: any) => {
  if (obj === null || !(obj instanceof Object)) {
      return obj;
  }
  var temp = (obj instanceof Array) ? [] : {};
  for (var key in obj) {
      temp[key] = cloneAsObject(obj[key]);
  }
  return temp;
}

export { cloneAsObject }
