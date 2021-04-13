import AmericanExpress from '@app/assets/cards/americanExpress.svg'
import DinersClub from '@app/assets/cards/dinersClub.svg'
import Discover from '@app/assets/cards/discover.svg'
import JCB from '@app/assets/cards/jcb.svg'
import MasterCard from '@app/assets/cards/masterCard.svg'
import UnionPay from '@app/assets/cards/unionPay.svg'
import Visa from '@app/assets/cards/visa.svg'

import { FractalPlan } from '@app/shared/types/payment'

export const CARDS: { [key: string]: any } = {
  'American Express': AmericanExpress,
  'Diners Club': DinersClub,
  Discover: Discover,
  JCB: JCB,
  MasterCard: MasterCard,
  UnionPay: UnionPay,
  Visa: Visa
}

export const STRIPE_OPTIONS = {
  fonts: [
    {
      cssSrc:
                'https://fonts.googleapis.com/css2?family=Josefin+Sans:wght@600&display=swap'
    }
  ]
}

// TODO: This should be stored in a database and pulled via GraphQL
// https://github.com/fractal/website/issues/337
export const PLANS: {
  [key: string]: {
    price: number
    name: FractalPlan
    subText: string
    details: string[]
  }
} = {
  Starter: {
    price: 50,
    name: FractalPlan.STARTER,
    subText: 'Additional 7-day free trial',
    details: ['Unlimited usage', 'Access to alpha features']
  },
  Pro: {
    price: 99,
    name: FractalPlan.PRO,
    subText: 'Coming Soon',
    details: ['Everything in Starter', 'Stream other browsers']
  }
}

export const TAX_RATES: { [key: string]: number } = {
  AL: 4,
  AK: 0,
  AZ: 5.6,
  AR: 6.5,
  CA: 7.25,
  CO: 2.9,
  CT: 6.35,
  DE: 0,
  DC: 5.75,
  FL: 6,
  GA: 4,
  ID: 6,
  IL: 6.25,
  IN: 7,
  IA: 6,
  KS: 6.5,
  KY: 6,
  LA: 4.45,
  ME: 5.5,
  MD: 6,
  MA: 6.25,
  MI: 6,
  MN: 6.875,
  MS: 7,
  MO: 4.225,
  MT: 0,
  NE: 5.5,
  NV: 6.85,
  NH: 0,
  NJ: 6.625,
  NM: 5.125,
  NY: 4,
  NC: 4.75,
  ND: 5,
  OH: 5.75,
  OK: 4.5,
  OR: 0,
  PA: 6,
  RI: 7,
  SC: 6,
  SD: 5,
  TN: 7,
  TX: 6.25,
  UT: 5.95,
  VT: 6,
  VA: 5.3,
  WA: 6.5,
  WV: 6,
  WI: 5,
  WY: 4
}
